from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Entry:
    kind: str
    path: Path
    label: str


def normalize(folder: Path) -> Path:
    path = Path(folder).expanduser()
    try:
        return path.resolve()
    except OSError:
        return path


def list_entries(folder: Path) -> list[Entry]:
    folder = normalize(folder)
    rows: list[Entry] = []
    parent = folder.parent
    if parent != folder:
        rows.append(Entry("up", parent, ".."))
    try:
        children = list(folder.iterdir())
    except OSError:
        return rows
    dirs: list[Entry] = []
    files: list[Entry] = []
    for child in children:
        try:
            is_dir = child.is_dir()
        except OSError:
            is_dir = False
        if is_dir:
            dirs.append(Entry("dir", child, child.name + "/"))
        else:
            files.append(Entry("file", child, child.name))
    dirs.sort(key=lambda entry: entry.label.casefold())
    files.sort(key=lambda entry: entry.label.casefold())
    rows.extend(dirs)
    rows.extend(files)
    return rows
