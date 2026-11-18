#!/usr/bin/env python3
"""RAM check for a cross-page Return. Does not save."""

import importlib.util
import json
import subprocess
import sys
import time

spec = importlib.util.spec_from_file_location("c64drv", "tools/c64u.py")
drv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(drv)

K_CTRL_G = 7


def fail(msg):
    print("FAIL", msg)
    drv.dump_rows(label="fail")
    drv.reset()
    sys.exit(1)


def row_bytes(scr, row):
    return bytes(scr[row * 40:(row + 1) * 40])


def lines_addr():
    with open("build/whisper64.map") as handle:
        for line in handle:
            parts = line.split()
            if len(parts) >= 2 and parts[-1] == "lines":
                return int(parts[0], 16)
    fail("lines not in map")


def screen():
    return drv.read_mem(drv.SCREEN, 1000)


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


def ram_row(addr, row):
    blob = drv.read_mem(addr + row * 80, 80)
    return bytes(blob).split(b"\x00", 1)[0]


def wait_row(row, needle, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if needle in row_bytes(screen(), row):
            return True
        time.sleep(0.3)
    return False


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
            "c64u", "drives", "mount", letter, "/SD/My Files/W64.d64"])
        time.sleep(1)
        return
    fail("drive 8 has a different image: " + image)


def boot():
    drv.reset()
    ensure_w64()
    drv.load_and_run(drv.PRG)
    scr = screen()
    if b"REU" in row_bytes(scr, 24):
        drv.inject_keys([0x20], settle=0.6)


def selected_row(col):
    for r in range(4, 22):
        if (col[r * 40 + 1] & 0x0F) == 5:
            return r
    return -1


def load_named(name):
    drv.inject_keys([drv.K_F1], settle=0.5)
    if not wait_row(0, b"DIRECTORY", 30):
        fail("directory")
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


def goto_line(n):
    codes = [K_CTRL_G] + [ord(ch) for ch in str(n)] + [drv.K_RETURN]
    drv.inject_keys(codes, settle=0.4)
    deadline = time.time() + 8
    while time.time() < deadline:
        scr = screen()
        if title_line(scr) == n and b"GOTO LINE" not in row_bytes(scr, 24):
            return scr
        time.sleep(0.2)
    fail("goto %d" % n)


def case_delete_middle():
    addr = lines_addr()
    boot()
    load_named("DICEROLL-SRC")
    goto_line(2)
    drv.inject_keys([drv.K_DELETE], settle=0.5)
    goto_line(48)
    row = ram_row(addr, 47)
    if not row.startswith(b"H6$ = B1$+CHR$(18)"):
        fail("page 0 did not pull H6 %r" % row[:40])
    goto_line(49)
    row = ram_row(addr, 0)
    if not row.startswith(b"H7$ = B1$+CHR$(18)"):
        fail("page 1 row 0 %r" % row[:40])
    drv.reset()


def case_delete_page_top():
    addr = lines_addr()
    boot()
    load_named("DICEROLL-SRC")
    goto_line(97)
    drv.inject_keys([drv.K_DELETE], settle=0.8)
    scr = screen()
    if title_line(scr) != 96:
        fail("page top join title %s" % title_line(scr))
    row = ram_row(addr, 47)
    if row != b"MENUNAV:":
        fail("joined line %r" % row)
    goto_line(97)
    row = ram_row(addr, 0)
    if not row.startswith(b"REM READ JOYSTICK INPUT"):
        fail("pulled row %r" % row[:40])
    if row[:1] != b"R":
        fail("pulled first byte %r" % row[:4])
    drv.reset()


def case_return():
    addr = lines_addr()
    boot()
    load_named("DICEROLL-SRC")
    goto_line(48)
    started = time.time()
    drv.inject_keys([drv.K_RETURN], settle=0.05)
    landed = None
    while time.time() - started < 2.0:
        if title_line(screen()) == 49:
            landed = time.time() - started
            break
        time.sleep(0.05)
    if landed is None:
        fail("return still on the old line after 2s")
    print("return landed in %.2fs" % landed)
    row0 = ram_row(addr, 0)
    row1 = ram_row(addr, 1)
    if not row0.startswith(b"H5$ = B1$+CHR$(18)"):
        fail("carried H5 %r" % row0[:40])
    if not row1.startswith(b"H6$ = B1$+CHR$(18)"):
        fail("following H6 %r" % row1[:40])
    goto_line(97)
    blob = drv.read_mem(addr, 48 * 80)
    if b"MENUNAV:" not in blob:
        fail("MENUNAV missing")
    if b"RENUNAV" in blob:
        fail("RENUNAV")
    if b"IF CS=1 THEN GOSUB RD:GOTO MENU:REM 1=ROLL" not in blob:
        fail("IF CS=1 missing")
    if b" F CS=1" in blob:
        fail("IF CS=1 lost its I")
    drv.reset()


if __name__ == "__main__":
    case_delete_middle()
    case_delete_page_top()
    case_return()