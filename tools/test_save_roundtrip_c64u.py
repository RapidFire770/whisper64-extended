#!/usr/bin/env python3
"""Load and save DICEROLL-SRC through the -Oz editor. Does not replace that file."""

import importlib.util
import json
import subprocess
import sys
import time

spec = importlib.util.spec_from_file_location("c64drv", "tools/c64u.py")
drv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(drv)

HOST = "/home/josh/Documents/Programming/C64/dice-roll/src/diceroll-src.seq"
K_F2 = 137
K_CTRL_G = 7
SAVE_NAME = b"OPTCHK"


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


def host_lines():
    data = open(HOST, "rb").read().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    lines = data.split(b"\n")
    if lines and lines[-1] == b"":
        lines.pop()
    return lines


def screen():
    return drv.read_mem(drv.SCREEN, 1000)


def title_line(scr):
    raw = bytes(b & 0x7F for b in scr[8:17]).decode("latin1")
    if ":" not in raw:
        return -1
    num = ""
    for ch in raw.split(":", 1)[0]:
        if ch.isdigit():
            num += ch
        elif num:
            break
    return int(num) if num else -1


def wait_row(row, needle, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if needle in row_bytes(screen(), row):
            return True
        time.sleep(0.3)
    return False


def wait_status(needles, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        row = row_bytes(screen(), 24)
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
            "c64u", "drives", "mount", letter, "/SD/My Files/W64.d64"])
        time.sleep(1)
        return
    fail("drive 8 has a different image: " + image)


def boot():
    drv.reset()
    ensure_w64()
    drv.load_and_run(drv.PRG)
    if b"REU" in row_bytes(screen(), 24):
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
            return
        time.sleep(0.2)
    fail("goto %d" % n)


def read_all(addr, n):
    got = []
    page = 0
    while len(got) < n:
        goto_line(page * 48 + 1)
        blob = bytes(drv.read_mem(addr, 48 * 80))
        take = min(48, n - len(got))
        for row in range(take):
            raw = blob[row * 80:(row + 1) * 80].split(b"\x00", 1)[0]
            got.append(raw)
        page += 1
        if page > 20:
            fail("too many pages")
    return got


def compare(label, got, want):
    if len(got) != len(want):
        fail("%s count %d want %d" % (label, len(got), len(want)))
    addr = lines_addr()
    for i, (g, w) in enumerate(zip(got, want), 1):
        if g != w:
            # A DMA read of the line buffer has come back with one stray
            # byte that was not in the line on the next read.
            goto_line(i)
            row = (i - 1) % 48
            blob = bytes(drv.read_mem(addr + row * 80, 80))
            g = blob.split(b"\x00", 1)[0]
            if g != w:
                fail("%s line %d got %r want %r" % (label, i, g[:60], w[:60]))
    print(label, "ok", len(got), "lines")


def save_as(name, current_len):
    # The bracketed label keeps the old name. The editable copy starts
    # at column 9 + len + 3. Only that field decides the saved name.
    field = 9 + current_len + 3
    drv.inject_keys([K_F2], settle=0.5)
    if not wait_row(24, b"SAVE AS", 10):
        fail("save prompt")
    drv.inject_keys([drv.K_DELETE] * 16, settle=0.45)
    row = row_bytes(screen(), 24)
    if row[field:field + current_len].strip(b" \x00"):
        fail("name still in the edit field %r" % row[field:field + 16])
    drv.inject_keys(list(name), settle=0.4)
    row = row_bytes(screen(), 24)
    if row[field:field + len(name)] != name:
        fail("typed name missing %r" % row[field:field + 16])
    if b"DICEROLL" in row[field:]:
        fail("edit field still has DICEROLL")
    drv.inject_keys([drv.K_RETURN], settle=0.4)
    hit = wait_status([b"OVERWRITE", b"SAVED", b"SAVE ERROR", b"CANCELLED"], 180)
    if hit == b"OVERWRITE":
        drv.inject_keys([ord("Y")], settle=0.4)
        hit = wait_status([b"SAVED", b"SAVE ERROR"], 180)
    if hit != b"SAVED":
        fail("save result %s" % hit)


def main():
    want = host_lines()
    if len(want) != 444:
        fail("host line count %d" % len(want))
    addr = lines_addr()
    boot()
    load_named("DICEROLL-SRC")
    compare("load", read_all(addr, 444), want)
    save_as(SAVE_NAME, 12)
    load_named("OPTCHK")
    compare("reload", read_all(addr, 444), want)
    goto_line(2)
    drv.inject_keys([ord("X")], settle=0.45)
    goto_line(97)
    drv.inject_keys([ord("Q")], settle=0.45)
    edited = list(want)
    edited[1] = b"X" + edited[1]
    edited[96] = b"Q" + edited[96]
    save_as(SAVE_NAME, 6)
    load_named("OPTCHK")
    compare("edited reload", read_all(addr, 444), edited)
    drv.reset()
    print("save round trip passed")


if __name__ == "__main__":
    main()
