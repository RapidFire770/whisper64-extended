#!/usr/bin/env python3
"""
C64 Ultimate REST-API driver.

Drives the running editor on the developer's C64U (http://192.168.100.193)
via the KERNAL keyboard buffer ($0277, count $C6), and reads back screen +
color RAM so we can SEE the per-keystroke state that py65 cannot reproduce.

Usage (see functions called from __main__):
    python3 tools/c64u.py repro        # reset, load, type 3 lines, dump
    python3 tools/c64u.py down N       # inject N cursor-downs, dump after each

Keyboard buffer: $0277 (10 bytes), count at $C6. The editor's main-loop GETIN
consumes them. One buffer load = up to 10 keys; the loop runs thousands of
times per frame, so we sleep briefly between injections to let it drain.

Screen-code note: UPPERCASE A-Z ($41-$5A) and space ($20) match PETSCII, but
digits/punctuation differ. We decode leniently (see decode_row).
"""
import sys
import time
import urllib.request

HOST = "http://192.168.100.193"
SCREEN = 0x0400
COLOR = 0xD800
KBUF = 0x0277
KCOUNT = 0xC6

# PETSCII key codes for the KERNAL keyboard buffer (what GETIN returns).
K_DOWN = 0x11
K_UP = 0x91
K_RETURN = 0x0D
K_HOME = 0x13


def _put(path, data=None):
    url = HOST + path
    req = urllib.request.Request(url, data=data, method="PUT")
    with urllib.request.urlopen(req, timeout=10) as r:
        return r.read()


def _get(path):
    url = HOST + path
    with urllib.request.urlopen(url, timeout=10) as r:
        return r.read()


def reset():
    _put("/v1/machine:reset")
    time.sleep(2.5)


def load_and_run(prg_path):
    with open(prg_path, "rb") as f:
        data = f.read()
    req = urllib.request.Request(HOST + "/v1/runners:run_prg",
                                 data=data, method="POST",
                                 headers={"Content-Type": "application/octet-stream"})
    with urllib.request.urlopen(req, timeout=15) as r:
        r.read()
    time.sleep(2.0)


def read_mem(addr, length):
    """Read `length` bytes at `addr`; return bytes (binary response)."""
    return _get(f"/v1/machine:readmem?address=0x{addr:X}&length={length}")


def write_mem(addr, hex_bytes):
    _put(f"/v1/machine:writemem?address=0x{addr:X}&data={hex_bytes}")


def inject_keys(codes, settle=0.35):
    """Push up to 10 PETSCII keycodes into the KERNAL keyboard buffer."""
    for chunk in range(0, len(codes), 10):
        batch = codes[chunk:chunk + 10]
        data = "".join(f"{c:02X}" for c in batch)
        write_mem(KBUF, data)
        write_mem(KCOUNT, f"{len(batch):02X}")
        time.sleep(settle)


def decode_screen_row(raw, row):
    """Decode 40 screen bytes for `row` to a readable string (lenient)."""
    base = row * 40
    out = []
    for b in raw[base:base + 40]:
        b &= 0xFF
        # Loose screen-code -> ASCII: space $20, A-Z $41-$5A pass through.
        if b == 0x20:
            out.append(" ")
        elif 0x41 <= b <= 0x5A:
            out.append(chr(b))
        elif 0x30 <= b <= 0x39:  # digits: screen codes $30-$39 == PETSCII digits
            out.append(chr(b))
        elif b == 0x00:
            out.append(".")
        elif 0x80 & b:  # reverse video (cursor) -> mark
            out.append("[#%02X]" % b)
        else:
            out.append("<%02X>" % b)
    return "".join(out)


def dump_rows(rows=range(3, 10), label=""):
    scr = read_mem(SCREEN, 25 * 40)
    col = read_mem(COLOR, 25 * 40)
    if label:
        print(f"--- {label} ---")
    for r in rows:
        text = decode_screen_row(scr, r)
        colors = " ".join(f"{col[r*40+i]&0x0F:X}" for i in range(40))
        print(f"  r{r:2d}: {text}")
    # [BREADCRUMB] read cassette-buffer trace left by edit_draw_current_line:
    # $033C=ROW $033D=START_ROW $033E=len $033F=buf[0] $0340=counter $0341=caller
    try:
        bc = read_mem(0x033C, 9)
        caller = {0xA1: "redraw_all", 0xB1: "eml_line_dirty(D=1)"}.get(bc[5], "?")
        print(f"  [BC] ROW={bc[0]} START_ROW={bc[1]} len={bc[2]} buf[0]=${bc[3]:02X} "
              f"calls={bc[4]} caller={caller}")
        print(f"  [BC@entry] edcl_is_row1={bc[6]} edcl_row0={bc[7]} edcl_row1={bc[8]}")
    except Exception as e:
        print(f"  [BC] read failed: {e}")
    # [BREADCRUMB] walk-step ring at $4D00 (ptr at $4D80): 4 bytes/step
    # [idx, scrr_before(|0x80 if row1), len, buf0].
    try:
        n = read_mem(0x4D80, 1)[0]
        steps = read_mem(0x4D00, min(n, 128))
        print(f"  [WALK] {n//4} steps:")
        for i in range(0, len(steps), 4):
            if i + 3 >= len(steps):
                break
            idx, scrr, ln, b0 = steps[i:i+4]
            row1 = " (row1)" if (scrr & 0x80) else ""
            scr = scrr & 0x7F
            print(f"    idx={idx} scr_before={scr}{row1} ->r{scr+3} len={ln} buf0=${b0:02X}")
    except Exception as e:
        print(f"  [WALK] read failed: {e}")
    # [BREADCRUMB] end-of-redraw snapshot of screen r5/r6/r7 into $4D90/$4DA4/$4DB8.
    try:
        d = lambda b: "".join(chr(x) if 0x41<=x<=0x5A else (" " if x==0x20 else f"<{x:02X}>") for x in b)
        print(f"  [SNAP@redraw_end] r5={d(read_mem(0x4D90,20))}")
        print(f"  [SNAP@redraw_end] r6={d(read_mem(0x4DA4,20))}")
        print(f"  [SNAP@redraw_end] r7={d(read_mem(0x4DB8,20))}")
        # walk-end snapshot (before current-line draw) at $4E00/$4E14/$4E28
        print(f"  [SNAP@walk_end]   r5={d(read_mem(0x4E00,20))}")
        print(f"  [SNAP@walk_end]   r6={d(read_mem(0x4E14,20))}")
        print(f"  [SNAP@walk_end]   r7={d(read_mem(0x4E28,20))}")
    except Exception as e:
        print(f"  [SNAP] read failed: {e}")
    print()


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "repro"
    if cmd == "repro":
        reset()
        load_and_run("basic-ide.prg")
        dump_rows(label="editor launched (empty)")
        # Type: REM SHORT (line 0), then a long POKE line (wraps), then a long
        # PRINT line (wraps). RETURN commits each line.
        # REM <space> SHORT
        line0 = list(b"REM SHORT")
        line1 = list(b"POKE 53280,0:POKE 53281,0:REM SET BACKGROUND AND BORDER COLOR TO BLACK")
        line2 = list(b"PRINT \"TEXT COLOR TO GREEN AND A LONG STRING WRAPPING\"")
        inject_keys(line0 + [K_RETURN], settle=0.6)
        dump_rows(label="after line 0 (REM SHORT)")
        inject_keys(line1 + [K_RETURN], settle=0.8)
        dump_rows(label="after line 1 (POKE, wraps)")
        inject_keys(line2 + [K_RETURN], settle=0.8)
        dump_rows(label="after line 2 (PRINT, wraps) -- cursor on line 2")
        # HOME to put cursor at top first? Cursor should already be near top.
        # The bug: cursor on line 0, press DOWN twice. First get to line 0:
        # After typing, cursor is on the phantom line below line 2. Move UP
        # three times to reach line 0.
        inject_keys([K_UP, K_UP, K_UP], settle=0.5)
        dump_rows(label="cursor moved UP x3 -> on line 0 (REM)")
        # Now the reported repro: DOWN, DOWN.
        inject_keys([K_DOWN], settle=0.5)
        dump_rows(label="DOWN #1 (REM -> POKE)")
        inject_keys([K_DOWN], settle=0.5)
        dump_rows(label="DOWN #2 (POKE -> PRINT) -- BUG MANIFESTS HERE")
    elif cmd == "down":
        n = int(sys.argv[2]) if len(sys.argv) > 2 else 1
        for i in range(n):
            inject_keys([K_DOWN], settle=0.5)
            dump_rows(label=f"DOWN #{i+1}")
    elif cmd == "up":
        n = int(sys.argv[2]) if len(sys.argv) > 2 else 1
        for i in range(n):
            inject_keys([K_UP], settle=0.5)
            dump_rows(label=f"UP #{i+1}")
    elif cmd == "home":
        inject_keys([K_HOME], settle=0.5)
        dump_rows(label="HOME")
    elif cmd == "dump":
        dump_rows()
    elif cmd == "resetrun":
        reset()
        load_and_run("basic-ide.prg")


if __name__ == "__main__":
    main()
