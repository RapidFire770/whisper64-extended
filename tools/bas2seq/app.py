from __future__ import annotations

from pathlib import Path

from rich.text import Text
from textual import events, on
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Vertical, VerticalScroll
from textual.screen import Screen
from textual.widgets import DirectoryTree, Footer, Input, Static

from bas2seq.engine import (
    load_path,
    mapping,
    prepare,
    resolve,
    supply,
    write_seq,
)
from bas2seq.highlight import spans


def rich_line(line: str) -> Text:
    out = Text()
    for piece, role in spans(line):
        style = ""
        if role == "statement":
            style = "magenta"
        elif role == "function":
            style = "blue"
        out.append(piece, style=style)
    return out


class Bas2SeqApp(App):
    TITLE = "BAS to SEQ"

    def __init__(self) -> None:
        super().__init__()
        self.session = None
        self.accepted: dict[int, bool] = {}
        self.out_path: Path | None = None
        self.outcome = None
        self.source_path: Path | None = None
        self.save_error: str | None = None

    def on_mount(self) -> None:
        self.push_screen(FileScreen())

    def reset_session(self) -> None:
        self.session = None
        self.accepted = {}
        self.out_path = None
        self.outcome = None
        self.source_path = None
        self.save_error = None

    def goto_file(self) -> None:
        self.reset_session()
        self.switch_screen(FileScreen())

    def after_prepare(self) -> None:
        if self.session.items:
            self.switch_screen(LineScreen(0))
        else:
            self.after_lines()

    def unresolved_entries(self) -> list[tuple[int, str]]:
        out: list[tuple[int, str]] = []
        for item in self.session.items:
            if item.replacement is None:
                out.append((item.line_no, item.phrase))
            elif not self.accepted.get(item.line_no, False):
                out.append((item.line_no, item.phrase))
        return out

    def after_lines(self) -> None:
        if self.unresolved_entries():
            self.switch_screen(UnresolvedScreen())
            return
        if self.session.renames:
            self.switch_screen(RenameScreen())
            return
        self.outcome = resolve(self.session, self.accepted, True)
        self.switch_screen(WarningScreen())

    def begin_after_save(self) -> None:
        self.save_error = None
        if self.session.pending:
            self.switch_screen(NameScreen(self.session.pending[0]))
            return
        prepare(self.session)
        self.after_prepare()


class FileScreen(Screen):
    BINDINGS = [
        Binding("escape", "quit_app", "Quit"),
    ]

    def compose(self) -> ComposeResult:
        yield DirectoryTree(str(Path.cwd()), id="tree")
        yield Static("", id="error")
        yield Footer()

    def action_quit_app(self) -> None:
        self.app.exit()

    @on(DirectoryTree.FileSelected)
    def on_tree_file(self, event: DirectoryTree.FileSelected) -> None:
        path = Path(str(event.path))
        if path.suffix.lower() != ".bas":
            return
        app: Bas2SeqApp = self.app  # type: ignore[assignment]
        app.source_path = path
        app.session = load_path(path)
        app.accepted = {}
        app.outcome = None
        app.out_path = None
        app.save_error = None
        err = self.query_one("#error", Static)
        if app.session.error:
            err.update(app.session.error)
            return
        err.update("")
        app.switch_screen(SaveScreen())


class SaveScreen(Screen):
    BINDINGS = [
        Binding("escape", "back", "Back"),
        Binding("y", "overwrite_yes", "Replace", show=False),
        Binding("n", "overwrite_no", "Keep", show=False),
    ]

    def __init__(self) -> None:
        super().__init__()
        self._confirm_overwrite = False

    def compose(self) -> ComposeResult:
        with Vertical():
            yield Static("Output directory:")
            yield Input(id="dir")
            yield Static("Output name:")
            yield Input(id="name")
            yield Static("", id="prompt")
            yield Static("", id="err")
        yield Footer()

    def on_mount(self) -> None:
        app: Bas2SeqApp = self.app  # type: ignore[assignment]
        src = Path(app.source_path)
        self.query_one("#dir", Input).value = str(src.parent)
        self.query_one("#name", Input).value = src.stem + ".seq"
        err = self.query_one("#err", Static)
        if app.save_error:
            err.update(app.save_error)
        else:
            err.update("")

    def action_back(self) -> None:
        self.app.goto_file()  # type: ignore[attr-defined]

    def action_overwrite_yes(self) -> None:
        if self._confirm_overwrite:
            self._confirm_overwrite = False
            self.query_one("#prompt", Static).update("")
            self._finish_path()

    def action_overwrite_no(self) -> None:
        if self._confirm_overwrite:
            self._confirm_overwrite = False
            self.query_one("#prompt", Static).update("")

    def output_path(self) -> Path:
        parent = Path(self.query_one("#dir", Input).value)
        name = self.query_one("#name", Input).value
        return parent / name

    def _finish_path(self) -> None:
        app: Bas2SeqApp = self.app  # type: ignore[assignment]
        app.out_path = self.output_path()
        if app.outcome is not None and app.outcome.lines is not None:
            app.switch_screen(WarningScreen())
            return
        app.begin_after_save()

    def try_submit(self) -> None:
        path = self.output_path()
        if path.exists():
            if not self._confirm_overwrite:
                self._confirm_overwrite = True
                self.query_one("#prompt", Static).update(
                    "File exists. Y to replace, N to stay."
                )
            return
        self._confirm_overwrite = False
        self.query_one("#prompt", Static).update("")
        self._finish_path()

    @on(Input.Submitted)
    def on_input_submitted(self, event: Input.Submitted) -> None:
        if event.input.id in ("dir", "name"):
            self.try_submit()


class NameScreen(Screen):
    BINDINGS = [
        Binding("escape", "back", "Back"),
    ]

    def __init__(self, old_name: str) -> None:
        super().__init__()
        self.old_name = old_name

    def compose(self) -> ComposeResult:
        with Vertical():
            yield Static(f"Rename label: {self.old_name}")
            yield Input(placeholder="new name", id="new")
            yield Static("", id="err")
        yield Footer()

    def on_mount(self) -> None:
        self.query_one("#new", Input).focus()

    def action_back(self) -> None:
        self.app.goto_file()  # type: ignore[attr-defined]

    @on(Input.Submitted)
    def on_new_submitted(self, event: Input.Submitted) -> None:
        if event.input.id != "new":
            return
        app: Bas2SeqApp = self.app  # type: ignore[assignment]
        err = supply(app.session, self.old_name, event.value)
        box = self.query_one("#err", Static)
        if err:
            box.update(err)
            return
        box.update("")
        if app.session.pending:
            app.switch_screen(NameScreen(app.session.pending[0]))
            return
        prepare(app.session)
        app.after_prepare()


class LineScreen(Screen):
    BINDINGS = [
        Binding("escape", "back", "Back"),
        Binding("y", "accept", "Yes", show=False),
        Binding("n", "reject", "No", show=False),
        Binding("enter", "continue_blocking", "Continue", show=False),
    ]

    def __init__(self, index: int) -> None:
        super().__init__()
        self.index = index

    def compose(self) -> ComposeResult:
        with Vertical():
            yield Static("", id="header")
            yield Static("", id="original")
            yield Static("", id="detail")
            yield VerticalScroll(id="replace_scroll")
            yield Static("", id="keys")
        yield Footer()

    def on_mount(self) -> None:
        app: Bas2SeqApp = self.app  # type: ignore[assignment]
        item = app.session.items[self.index]
        self.query_one("#header", Static).update(f"Line {item.line_no}")
        self.query_one("#original", Static).update(rich_line(item.original))
        scroll = self.query_one("#replace_scroll", VerticalScroll)
        scroll.remove_children()
        if item.replacement is None:
            self.query_one("#detail", Static).update(f"{item.phrase}\n{item.detail}")
            self.query_one("#keys", Static).update("Enter to continue. Escape to abort.")
        else:
            self.query_one("#detail", Static).update(item.detail)
            for rep in item.replacement:
                scroll.mount(Static(rich_line(rep)))
            self.query_one("#keys", Static).update(
                "Y to accept, N to reject. Escape to abort."
            )

    def action_back(self) -> None:
        self.app.goto_file()  # type: ignore[attr-defined]

    def _next(self) -> None:
        app: Bas2SeqApp = self.app  # type: ignore[assignment]
        nxt = self.index + 1
        if nxt < len(app.session.items):
            app.switch_screen(LineScreen(nxt))
        else:
            app.after_lines()

    def action_accept(self) -> None:
        app: Bas2SeqApp = self.app  # type: ignore[assignment]
        item = app.session.items[self.index]
        if item.replacement is None:
            return
        app.accepted[item.line_no] = True
        self._next()

    def action_reject(self) -> None:
        app: Bas2SeqApp = self.app  # type: ignore[assignment]
        item = app.session.items[self.index]
        if item.replacement is None:
            return
        app.accepted[item.line_no] = False
        self._next()

    def action_continue_blocking(self) -> None:
        app: Bas2SeqApp = self.app  # type: ignore[assignment]
        item = app.session.items[self.index]
        if item.replacement is None:
            self._next()


class UnresolvedScreen(Screen):
    def compose(self) -> ComposeResult:
        yield Static("Unresolved lines:", id="title")
        with VerticalScroll(id="list"):
            pass
        yield Static("Press a key to return to files.", id="hint")
        yield Footer()

    def on_mount(self) -> None:
        app: Bas2SeqApp = self.app  # type: ignore[assignment]
        box = self.query_one("#list", VerticalScroll)
        for line_no, phrase in app.unresolved_entries():
            box.mount(Static(f"{line_no}: {phrase}"))

    def action_back(self) -> None:
        self.app.goto_file()  # type: ignore[attr-defined]

    def on_key(self, event: events.Key) -> None:
        event.stop()
        self.action_back()


class RenameScreen(Screen):
    BINDINGS = [
        Binding("escape", "back", "Back"),
        Binding("y", "accept", "Yes", show=False),
        Binding("n", "reject", "No", show=False),
    ]

    def compose(self) -> ComposeResult:
        yield Static("Label renames:", id="title")
        with VerticalScroll(id="list"):
            pass
        yield Static("Y to accept map, N to reject.", id="hint")
        yield Footer()

    def on_mount(self) -> None:
        app: Bas2SeqApp = self.app  # type: ignore[assignment]
        names = mapping(app.session)
        box = self.query_one("#list", VerticalScroll)
        for old, _new in app.session.renames:
            new = names[old]
            box.mount(Static(f"{old} -> {new}"))
            for line_no in app.session.uses.get(old, []):
                if 1 <= line_no <= len(app.session.source):
                    src = app.session.source[line_no - 1]
                    box.mount(Static(rich_line(src)))

    def action_back(self) -> None:
        self.app.goto_file()  # type: ignore[attr-defined]

    def action_accept(self) -> None:
        app: Bas2SeqApp = self.app  # type: ignore[assignment]
        app.outcome = resolve(app.session, app.accepted, True)
        app.switch_screen(WarningScreen())

    def action_reject(self) -> None:
        self.app.goto_file()  # type: ignore[attr-defined]


class WarningScreen(Screen):
    BINDINGS = [
        Binding("escape", "back", "Back"),
        Binding("w", "write", "Write", show=False),
    ]

    def compose(self) -> ComposeResult:
        yield Static("Warnings:", id="title")
        with VerticalScroll(id="list"):
            pass
        yield Static("", id="err")
        yield Static("W to write SEQ. Escape to abort.", id="hint")
        yield Footer()

    def on_mount(self) -> None:
        app: Bas2SeqApp = self.app  # type: ignore[assignment]
        box = self.query_one("#list", VerticalScroll)
        warnings = app.outcome.warnings if app.outcome else []
        if warnings:
            for note in warnings:
                box.mount(Static(note))
        else:
            box.mount(Static("(none)"))

    def action_back(self) -> None:
        self.app.goto_file()  # type: ignore[attr-defined]

    def action_write(self) -> None:
        app: Bas2SeqApp = self.app  # type: ignore[assignment]
        try:
            write_seq(app.outcome.lines, app.out_path)
        except (OSError, UnicodeEncodeError) as err:
            app.save_error = str(err)
            app.switch_screen(SaveScreen())
            return
        app.switch_screen(DoneScreen())


class DoneScreen(Screen):
    def compose(self) -> ComposeResult:
        yield Static("", id="summary")
        yield Static("Press a key to return to files.", id="hint")
        yield Footer()

    def on_mount(self) -> None:
        app: Bas2SeqApp = self.app  # type: ignore[assignment]
        lines = len(app.outcome.lines) if app.outcome and app.outcome.lines else 0
        warns = len(app.outcome.warnings) if app.outcome else 0
        self.query_one("#summary", Static).update(
            f"Wrote {app.out_path}\n{lines} lines, {warns} warnings."
        )

    def action_back(self) -> None:
        self.app.goto_file()  # type: ignore[attr-defined]

    def on_key(self, event: events.Key) -> None:
        event.stop()
        self.action_back()


def main() -> None:
    Bas2SeqApp().run()

