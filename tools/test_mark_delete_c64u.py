#!/usr/bin/env python3
"""DEL with a mark on must drop the marked editor lines."""

import importlib.util
import json
import subprocess
import sys
import time

spec = importlib.util.spec_from_file_location("c64drv", "tools/c64u.py")
drv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(drv)

HOST = "/home/josh/Documents/Programming/C64/dice-roll/src/diceroll-src.seq"
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


def read_page(addr):
    first = bytes(drv.read_mem(addr, 48 * 80))
    second = bytes(drv.read_mem(addr, 48 * 80))
    if first == second:
        return first
    third = bytes(drv.read_mem(addr, 48 * 80))
    if third == first or third == second:
        return third
    return third


def read_all(addr, n):
    got = []
    page = 0
    while len(got) < n:
        goto_line(page * 48 + 1)
        blob = read_page(addr)
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
    for i, (g, w) in enumerate(zip(got, want), 1):
        if g != w:
            fail("%s line %d got %r want %r" % (label, i, g[:60], w[:60]))
    print(label, "ok", len(got), "lines")


def main():
    want = host_lines()
    addr = lines_addr()
    boot()
    load_named("DICEROLL-SRC")
    goto_line(1)
    drv.inject_keys([drv.K_RIGHT, drv.K_DELETE], settle=0.5)
    row = bytes(drv.read_mem(addr, 80)).split(b"\x00", 1)[0]
    if row != b"EM === MAIN INITIALIZATION ===":
        fail("char delete %r" % row[:40])
    boot()
    load_named("DICEROLL-SRC")
    goto_line(2)
    drv.inject_keys([drv.K_CTRL_K, drv.K_DOWN, drv.K_DOWN, drv.K_DELETE], settle=4.0)
    deadline = time.time() + 8
    expect = want[:1] + want[4:]
    got = None
    while time.time() < deadline:
        goto_line(2)
        row = bytes(drv.read_mem(addr + 80, 80)).split(b"\x00", 1)[0]
        if row == expect[1]:
            got = read_all(addr, len(expect))
            break
        time.sleep(0.3)
    if got is None:
        fail("marked lines still present")
    compare("mark delete", got, expect)
    drv.reset()
    print("mark delete passed")


if __name__ == "__main__":
    main()
