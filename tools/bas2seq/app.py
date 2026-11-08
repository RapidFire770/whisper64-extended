from __future__ import annotations

from pathlib import Path

from rich.text import Text
from textual import events, on
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Vertical, VerticalScroll
from textual.message import Message
from textual.screen import Screen
from textual.widgets import Footer, Input, ListItem, ListView, Static

from bas2seq.browser import Entry, list_entries, normalize
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
    CSS = """
    Screen > Vertical {
        height: 1fr;
    }
    DirList {
        height: 1fr;
    }
    #where {
        height: 1;
    }
    """

    def __init__(self) -> None:
        super().__init__()
        self.session = None
        self.accepted: dict[int, bool] = {}
        self.out_path: Path | None = None
        self.outcome = None
        self.source_path: Path | None = None
        self.save_error: str | None = None
        self.file_start: Path | None = None

    def on_mount(self) -> None:
        self.push_screen(FileScreen(self.file_start))

    def reset_session(self) -> None:
        self.session = None
        self.accepted = {}
        self.out_path = None
        self.outcome = None
        self.source_path = None
        self.save_error = None

    def goto_file(self) -> None:
        self.reset_session()
        self.switch_screen(FileScreen(self.file_start))

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


class DirRow(ListItem):
    def __init__(self, entry: Entry) -> None:
        super().__init__(Static(entry.label))
        self.entry = entry


class DirList(ListView):
    class Moved(Message):
        def __init__(self, folder: Path) -> None:
            super().__init__()
            self.folder = folder

    class Opened(Message):
        def __init__(self, path: Path) -> None:
            super().__init__()
            self.path = path

    def __init__(self, start: Path, **kwargs) -> None:
        super().__init__(**kwargs)
        self.folder = normalize(start)

    async def on_mount(self) -> None:
        await self.show_folder(self.folder)

    async def show_folder(self, folder: Path) -> None:
        self.folder = normalize(folder)
        entries = list_entries(self.folder)
        await self.clear()
        if entries:
            await self.mount(*(DirRow(entry) for entry in entries))
            self.index = 0
        self.post_message(self.Moved(self.folder))

    def _on_list_item__child_clicked(self, event: ListItem._ChildClicked) -> None:
        # clear() removes the row before a click on it is handled.
        if event.item not in self._nodes:
            event.stop()
            return
        super()._on_list_item__child_clicked(event)

    async def on_list_view_selected(self, event: ListView.Selected) -> None:
        event.stop()
        item = event.item
        if not isinstance(item, DirRow):
            return
        if item.entry.kind == "file":
            self.post_message(self.Opened(item.entry.path))
            return
        await self.show_folder(item.entry.path)


class FileScreen(Screen):
    BINDINGS = [
        Binding("escape", "quit_app", "Quit"),
    ]

    def __init__(self, start: Path | None = None) -> None:
        super().__init__()
        self.start = start if start is not None else Path.cwd()

    def compose(self) -> ComposeResult:
        yield Static(str(normalize(self.start)), id="where")
        yield DirList(self.start, id="tree")
        yield Static("", id="error")
        yield Footer()

    def on_dir_list_moved(self, event: DirList.Moved) -> None:
        self.query_one("#where", Static).update(str(event.folder))

    def on_mount(self) -> None:
        self.query_one(DirList).focus()

    def action_quit_app(self) -> None:
        self.app.exit()

    def on_dir_list_opened(self, event: DirList.Opened) -> None:
        path = event.path
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


class SaveInput(Input):
    def check_consume_key(self, key: str, character: str | None) -> bool:
        screen = self.screen
        if (
            isinstance(screen, SaveScreen)
            and screen._confirm_overwrite
            and key.lower() in ("y", "n")
        ):
            return False
        return super().check_consume_key(key, character)


class SaveScreen(Screen):
    BINDINGS = [
        Binding("escape", "back", "Back"),
        Binding("y", "overwrite_yes", "Replace", show=False, priority=True),
        Binding("Y", "overwrite_yes", "Replace", show=False, priority=True),
        Binding("n", "overwrite_no", "Keep", show=False, priority=True),
        Binding("N", "overwrite_no", "Keep", show=False, priority=True),
    ]

    def __init__(self) -> None:
        super().__init__()
        self._confirm_overwrite = False
        self._browser_fill: tuple[str, str] | None = None
        self._listed_dir: str | None = None

    def compose(self) -> ComposeResult:
        app: Bas2SeqApp = self.app  # type: ignore[assignment]
        start = Path.cwd()
        name = ""
        if app.source_path is not None:
            src = Path(app.source_path)
            start = src.parent
            name = src.stem + ".seq"
        with Vertical():
            yield Static(str(normalize(start)), id="where")
            yield DirList(start, id="browser")
            yield Static("Output directory:")
            yield SaveInput(str(normalize(start)), id="dir")
            yield Static("Output name:")
            yield SaveInput(name, id="name")
            yield Static("", id="prompt")
            yield Static("", id="err")
        yield Footer()

    def on_mount(self) -> None:
        app: Bas2SeqApp = self.app  # type: ignore[assignment]
        src = Path(app.source_path)
        directory = self.query_one("#dir", Input)
        name = self.query_one("#name", Input)
        if directory.value == "":
            directory.value = str(normalize(src.parent))
        if name.value == "":
            name.value = src.stem + ".seq"
        err = self.query_one("#err", Static)
        if app.save_error:
            err.update(app.save_error)
        else:
            err.update("")
        self.query_one(DirList).focus()

    def action_back(self) -> None:
        self.app.goto_file()  # type: ignore[attr-defined]

    def check_action(self, action: str, parameters: tuple[object, ...]) -> bool | None:
        if action in ("overwrite_yes", "overwrite_no"):
            return self._confirm_overwrite
        return True

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

    def _clear_overwrite(self) -> None:
        self._confirm_overwrite = False
        self.query_one("#prompt", Static).update("")

    def try_submit(self) -> None:
        name = self.query_one("#name", Input).value
        if name.strip() == "":
            self._clear_overwrite()
            self.query_one("#err", Static).update("Enter a file name.")
            return
        self.query_one("#err", Static).update("")
        path = self.output_path()
        if path.exists():
            if not self._confirm_overwrite:
                self._confirm_overwrite = True
                self.query_one("#prompt", Static).update(
                    "File exists. Y to replace, N to stay."
                )
            return
        self._clear_overwrite()
        self._finish_path()

    def on_dir_list_moved(self, event: DirList.Moved) -> None:
        folder = str(event.folder)
        self.query_one("#where", Static).update(folder)
        field = self.query_one("#dir", Input)
        if self._listed_dir is None and field.value not in ("", folder):
            self._listed_dir = folder
            return
        field.value = folder
        self._listed_dir = folder

    def on_dir_list_opened(self, event: DirList.Opened) -> None:
        directory = str(event.path.parent)
        name = event.path.name
        self._browser_fill = (directory, name)
        self.query_one("#dir", Input).value = directory
        self.query_one("#name", Input).value = name
        self.try_submit()

    @on(Input.Changed)
    def on_input_changed(self, event: Input.Changed) -> None:
        if event.input.id not in ("dir", "name"):
            return
        if self._browser_fill is not None:
            directory = self.query_one("#dir", Input).value
            name = self.query_one("#name", Input).value
            if (directory, name) == self._browser_fill:
                return
            self._browser_fill = None
        self._clear_overwrite()

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

