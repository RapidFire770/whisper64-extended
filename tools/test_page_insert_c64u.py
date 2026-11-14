#!/usr/bin/env python3
"""40-column check that Return on a full page carries the last row forward."""

import importlib.util
import json
import subprocess
import sys
import time

spec = importlib.util.spec_from_file_location("c64drv", "tools/c64u.py")
drv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(drv)

K_F2 = 137
K_CTRL_N = 14
K_CTRL_G = 7
BLANK = b" " * 37


def fail(msg):
    print("FAIL", msg)
    drv.dump_rows(label="fail")
    sys.exit(1)


def row_bytes(scr, row):
    return bytes(scr[row * 40:(row + 1) * 40])


def wait_row(row, needle, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        scr = drv.read_mem(drv.SCREEN, 1000)
        if needle in row_bytes(scr, row):
            return True
        time.sleep(0.3)
    return False


def wait_status(needles, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        scr = drv.read_mem(drv.SCREEN, 1000)
        row = row_bytes(scr, 24)
        for needle in needles:
            if needle in row:
                return needle
        time.sleep(0.3)
    return None


def ensure_w64():
    raw = subprocess.check_output(["c64u", "drives", "list", "--json"], text=True)
    data = json.loads(raw)
    entries = data["drives"] if isinstance(data, dict) and "drives" in data else data
    found = None
    for item in entries:
        for letter, info in item.items():
            if str(info.get("bus_id")) == "8":
                found = (letter, info)
    if found is None:
        fail("no drive with bus_id 8")
    letter, info = found
    image = " ".join(str(info.get(k) or "") for k in ("image_file", "image_path"))
    if "W64.d64" in image:
        return
    if image.strip() == "":
        subprocess.check_call([
            "c64u", "drives", "mount", letter, "USB1/MY FILES/W64.d64"])
        time.sleep(1)
        return
    fail("drive 8 has a different image: " + image)


def boot():
    drv.reset()
    ensure_w64()
    drv.load_and_run(drv.PRG)
    scr = drv.read_mem(drv.SCREEN, 1000)
    if b"REU" in row_bytes(scr, 24):
        drv.inject_keys([0x20], settle=0.6)


def cursor_row(scr):
    for i, b in enumerate(scr):
        if b & 0x80:
            return i // 40
    return -1


def title_field(scr, after_colon):
    raw = bytes(b & 0x7F for b in scr[8:17]).decode("latin1")
    if ":" not in raw:
        return -1
    body = raw.split(":", 1)[1] if after_colon else raw.split(":", 1)[0]
    num = ""
    for ch in body:
        if ch.isdigit():
            num += ch
        elif num:
            break
    return int(num) if num else -1


def title_line(scr):
    return title_field(scr, False)


def title_col(scr):
    return title_field(scr, True)


def page_pair(scr):
    raw = bytes(b & 0x7F for b in scr[22:40]).decode("latin1")
    if "P" not in raw or "/" not in raw:
        return None
    body = raw[raw.index("P") + 1:]
    left, right = body.split("/", 1)

    def digits(s):
        out = ""
        for ch in s:
            if ch.isdigit():
                out += ch
            elif out:
                break
        return int(out) if out else None

    a = digits(left)
    b = digits(right)
    if a is None or b is None:
        return None
    return a, b


def line_text(scr):
    r = cursor_row(scr)
    if r < 0:
        scr = wait_cursor()
        r = cursor_row(scr)
    if r < 0:
        fail("no cursor")
    return bytes((scr[r * 40 + c] & 0x7F) for c in range(3, 40))


def gutter(scr):
    r = cursor_row(scr)
    if r < 0:
        scr = wait_cursor()
        r = cursor_row(scr)
    if r < 0:
        fail("no cursor")
    return bytes((scr[r * 40 + c] & 0x7F) for c in (0, 1))


def screen():
    return drv.read_mem(drv.SCREEN, 1000)


def wait_cursor(seconds=3):
    deadline = time.time() + seconds
    scr = screen()
    while time.time() < deadline:
        if cursor_row(scr) >= 0:
            return scr
        time.sleep(0.1)
        scr = screen()
    return scr


def goto_line(n):
    codes = [K_CTRL_G] + [ord(ch) for ch in str(n)] + [drv.K_RETURN]
    drv.inject_keys(codes, settle=0.4)
    deadline = time.time() + 5
    while time.time() < deadline:
        scr = screen()
        if title_line(scr) == n and b"GOTO LINE" not in row_bytes(scr, 24):
            return scr
        time.sleep(0.2)
    fail("goto %d" % n)


def read_line(n):
    return line_text(goto_line(n))


def selected_row(col):
    for r in range(4, 22):
        if (col[r * 40 + 1] & 0x0F) == 5:
            return r
    return -1


def load_named(name):
    drv.inject_keys([drv.K_F1], settle=0.5)
    if not wait_row(0, b"DIRECTORY", 30):
        if wait_row(24, b"NO FILES", 1) or wait_row(24, b"DIR ERROR", 1):
            fail("directory")
        fail("directory timeout")
    target = name.encode("ascii")
    for _ in range(40):
        scr = screen()
        col = drv.read_mem(drv.COLOR, 1000)
        row = selected_row(col)
        if row >= 0 and target in row_bytes(scr, row):
            drv.inject_keys([drv.K_RETURN], settle=0.5)
            if not wait_row(24, b"LOADED", 90):
                fail("load " + name)
            return
        drv.inject_keys([drv.K_DOWN], settle=0.3)
    fail("find " + name)


def save_diceins():
    drv.inject_keys([K_F2], settle=0.5)
    if not wait_row(24, b"SAVE AS", 10):
        fail("save prompt")
    drv.inject_keys([drv.K_DELETE] * 12, settle=0.35)
    drv.inject_keys(list(b"DICEINS") + [drv.K_RETURN], settle=0.5)
    hit = wait_status([b"OVERWRITE", b"SAVED"], 120)
    if hit == b"OVERWRITE":
        drv.inject_keys([ord("Y")], settle=0.5)
        hit = wait_status([b"SAVED"], 120)
    if hit != b"SAVED":
        fail("saved")


def status_idle(st):
    body = bytes(b & 0x7F for b in st)
    if b"LOADED" in body or b"GOTO LINE" in body:
        return False
    if body.startswith(b"LINE "):
        return False
    return True


def to_line_end():
    # One right redraws a wrapped line slower than the keyboard buffer
    # accepts the next ten, so a bulk right stops mid-line.
    scr = screen()
    col = title_col(scr)
    for _ in range(80):
        drv.inject_keys([drv.K_RIGHT], settle=0.05)
        deadline = time.time() + 1.5
        nxt = col
        while time.time() < deadline:
            scr = screen()
            nxt = title_col(scr)
            if nxt != col:
                break
            time.sleep(0.05)
        if nxt == col:
            return scr
        col = nxt
    fail("end of line")


def press_return():
    drv.inject_keys([drv.K_RETURN], settle=0.8)
    deadline = time.time() + 120
    while time.time() < deadline:
        if status_idle(row_bytes(screen(), 24)):
            time.sleep(0.5)
            if status_idle(row_bytes(screen(), 24)):
                return
        time.sleep(0.4)
    fail("return busy")


def case_middle():
    load_named("DICEROLL-SRC")
    a = read_line(1)
    b = read_line(48)
    c = read_line(49)
    goto_line(1)
    press_return()
    if read_line(1) != BLANK:
        fail("middle line 1")
    if read_line(2) != a:
        fail("middle line 2")
    if read_line(49) != b:
        fail("middle line 49")
    if read_line(50) != c:
        fail("middle line 50")
    save_diceins()
    load_named("DICEINS")
    if read_line(1) != BLANK or read_line(2) != a:
        fail("reloaded DICEINS")


def case_last_row():
    load_named("DICEROLL-SRC")
    a = read_line(48)
    b = read_line(49)
    goto_line(48)
    to_line_end()
    press_return()
    scr = screen()
    if title_line(scr) != 49 or line_text(scr) != BLANK:
        fail("last-row cursor")
    if read_line(48) != a:
        fail("last-row kept")
    if read_line(50) != b:
        fail("last-row pushed")


def to_last_page():
    scr = wait_cursor()
    prev = page_pair(scr)
    for _ in range(20):
        drv.inject_keys([K_CTRL_N], settle=0.15)
        deadline = time.time() + 8
        cur = prev
        while time.time() < deadline:
            scr = screen()
            cur = page_pair(scr)
            if cur != prev and cur is not None and cursor_row(scr) >= 0:
                break
            time.sleep(0.15)
        else:
            return wait_cursor()
        prev = cur
    fail("last page")


def to_last_row(scr):
    prev = gutter(scr)
    for _ in range(48):
        drv.inject_keys([drv.K_DOWN], settle=0.25)
        scr = screen()
        cur = gutter(scr)
        if cur == prev:
            return scr
        prev = cur
    fail("last row")


def case_end():
    load_named("DICEROLL-SRC")
    scr = to_last_row(to_last_page())
    digits = gutter(scr)
    before = page_pair(scr)
    if digits != b"47":
        for _ in range(48):
            if gutter(scr) == b"00":
                break
            drv.inject_keys([drv.K_UP], settle=0.25)
            scr = screen()
        press_return()
        scr = screen()
        if page_pair(scr) != before:
            fail("short page grew")
        return
    text = line_text(scr)
    number = title_line(scr)
    to_line_end()
    press_return()
    scr = screen()
    after = page_pair(scr)
    if after is None or before is None or after[1] != before[1] + 1:
        fail("new page count")
    if gutter(scr) != b"00" or line_text(scr) != BLANK:
        fail("new page row")
    if read_line(number) != text:
        fail("old last row")


def main():
    boot()
    case_middle()
    case_last_row()
    case_end()
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())