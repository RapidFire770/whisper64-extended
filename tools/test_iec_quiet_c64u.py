#!/usr/bin/env python3
"""Round-trip DICEROLL-BACKUP at 1 MHz and at 4 MHz. Does not write W64.d64."""

import importlib.util
import json
import os
import subprocess
import sys
import time

spec = importlib.util.spec_from_file_location("c64drv", "tools/c64u.py")
drv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(drv)

K_F2 = 137
K_CTRL_G = 7
RESTORING = False
ORIG = {}


def fail(msg):
    print("FAIL", msg, flush=True)
    try:
        drv.dump_rows(label="fail")
    except Exception as exc:
        print("screen unread", exc, flush=True)
    restore_machine()
    sys.exit(1)


def restore_machine():
    global RESTORING
    if RESTORING or not ORIG.get("touched"):
        return
    RESTORING = True
    print("restoring machine", flush=True)
    failed = []
    try:
        drv.reset()
    except Exception as exc:
        failed.append("reset: %s" % exc)
    if ORIG.get("remote") and ORIG.get("letter"):
        rc = subprocess.call([
            "c64u", "drives", "mount", ORIG["letter"], ORIG["remote"],
            "--mode", "readwrite"])
        if rc != 0:
            failed.append("mount")
    if ORIG.get("speed"):
        rc = subprocess.call([
            "c64u", "config", "set", "U64 Specific Settings",
            "CPU Speed", ORIG["speed"]])
        if rc != 0:
            failed.append("CPU Speed")
    if ORIG.get("turbo"):
        rc = subprocess.call([
            "c64u", "config", "set", "U64 Specific Settings",
            "Turbo Control", ORIG["turbo"]])
        if rc != 0:
            failed.append("Turbo Control")
    if failed:
        print("restore failed", " ".join(failed), flush=True)
        sys.exit(1)


def sectors_on(track):
    if track <= 17:
        return 21
    if track <= 24:
        return 19
    if track <= 30:
        return 18
    return 17


def sector_offset(track, sector):
    n = 0
    t = 1
    while t < track:
        n += sectors_on(t)
        t += 1
    return (n + sector) * 256


def blank_d64():
    data = bytearray(174848)
    off = sector_offset(18, 0)
    data[off] = 18
    data[off + 1] = 1
    data[off + 2] = 0x41
    name = b"IEC QUIET"
    i = 0
    while i < 16:
        data[off + 0x90 + i] = name[i] if i < len(name) else 0xA0
        i += 1
    data[off + 0xA0] = 0xA0
    data[off + 0xA1] = 0xA0
    data[off + 0xA2] = ord("6")
    data[off + 0xA3] = ord("4")
    data[off + 0xA4] = 0xA0
    data[off + 0xA5] = ord("2")
    data[off + 0xA6] = ord("A")
    track = 1
    while track <= 35:
        base = off + 4 + (track - 1) * 4
        count = sectors_on(track)
        data[base] = count
        sector = 0
        while sector < count:
            data[base + 1 + (sector // 8)] |= 1 << (sector % 8)
            sector += 1
        track += 1
    return data


def allocate(data, track, sector):
    off = sector_offset(18, 0)
    base = off + 4 + (track - 1) * 4
    pos = base + 1 + (sector // 8)
    bit = 1 << (sector % 8)
    if data[pos] & bit:
        data[pos] &= (0xFF ^ bit)
        if data[base]:
            data[base] -= 1


def chain_for(nbytes):
    need = (nbytes + 253) // 254
    if need < 1:
        need = 1
    spots = []
    track = 1
    sector = 0
    while len(spots) < need:
        if track == 18:
            track = 19
            sector = 0
        if track > 35:
            raise RuntimeError("file does not fit")
        if sector >= sectors_on(track):
            track += 1
            sector = 0
            continue
        spots.append((track, sector))
        sector += 1
    return spots


def write_seq(image, disk_name, payload):
    spots = chain_for(len(payload))
    allocate(image, 18, 0)
    allocate(image, 18, 1)
    for track, sector in spots:
        allocate(image, track, sector)
    doff = sector_offset(18, 1)
    image[doff] = 0
    image[doff + 1] = 0xFF
    entry = doff + 2
    image[entry] = 0x82
    image[entry + 1] = spots[0][0]
    image[entry + 2] = spots[0][1]
    i = 0
    while i < 16:
        image[entry + 3 + i] = disk_name[i] if i < len(disk_name) else 0xA0
        i += 1
    image[entry + 28] = len(spots) & 0xFF
    image[entry + 29] = (len(spots) >> 8) & 0xFF
    pos = 0
    for i, (track, sector) in enumerate(spots):
        soff = sector_offset(track, sector)
        chunk = payload[pos:pos + 254]
        pos += len(chunk)
        if i + 1 == len(spots):
            image[soff] = 0
            image[soff + 1] = len(chunk) + 1
        else:
            image[soff] = spots[i + 1][0]
            image[soff + 1] = spots[i + 1][1]
        image[soff + 2:soff + 2 + len(chunk)] = chunk
    if pos != len(payload):
        raise RuntimeError("short write")


def files_in(image):
    out = []
    track, sector = 18, 1
    seen = set()
    while track and (track, sector) not in seen:
        seen.add((track, sector))
        off = sector_offset(track, sector)
        nxt, nsec = image[off], image[off + 1]
        slot = 0
        while slot < 8:
            entry = off + 2 + slot * 32
            kind = image[entry]
            if kind != 0:
                raw = bytes(image[entry + 3:entry + 19])
                name = raw.split(b"\xa0", 1)[0]
                start = (image[entry + 1], image[entry + 2])
                out.append((name, kind, start))
            slot += 1
        if nxt == 0:
            break
        track, sector = nxt, nsec
    return out


def read_chain(image, start):
    track, sector = start
    seen = set()
    blob = bytearray()
    while track and (track, sector) not in seen:
        seen.add((track, sector))
        off = sector_offset(track, sector)
        nxt, used = image[off], image[off + 1]
        if nxt == 0:
            take = used - 1
            if take < 0:
                take = 0
            blob += image[off + 2:off + 2 + take]
            break
        blob += image[off + 2:off + 256]
        track, sector = nxt, used
    return bytes(blob)


def payload_named(image, name):
    want = name if isinstance(name, bytes) else name.encode("ascii")
    for entry, kind, start in files_in(image):
        if entry == want and (kind & 0x0F) in (1, 2):
            return read_chain(image, start)
    return None


def remote_of(info):
    path = str(info.get("image_path") or "")
    name = str(info.get("image_file") or "")
    if path.endswith("/"):
        return path + name
    if path and name:
        return path + "/" + name
    return name


def find_drive_8():
    raw = subprocess.check_output(["c64u", "drives", "list", "--json"], text=True)
    data = json.loads(raw)
    entries = data["drives"] if isinstance(data, dict) and "drives" in data else data
    for item in entries:
        for letter, info in item.items():
            if str(info.get("bus_id")) == "8":
                return letter, info
    fail("no drive with bus_id 8")


def item_node(item):
    raw = subprocess.check_output(
        ["c64u", "config", "get", "U64 Specific Settings", item, "--json"],
        text=True)
    data = json.loads(raw)
    if isinstance(data, dict) and "U64 Specific Settings" in data:
        data = data["U64 Specific Settings"]
    if isinstance(data, dict) and item in data:
        data = data[item]
    return data


def current_of(node):
    if isinstance(node, dict) and "current" in node:
        return str(node["current"])
    return str(node)


def option_strings(node):
    found = []
    if not isinstance(node, dict):
        return found
    for key in ("options", "values", "choices", "allowed"):
        value = node.get(key)
        if not isinstance(value, list):
            continue
        for entry in value:
            if isinstance(entry, str):
                found.append(entry)
            elif isinstance(entry, dict):
                for field in ("value", "name", "label"):
                    if isinstance(entry.get(field), str):
                        found.append(entry[field])
                        break
    return found


def set_speed(want):
    node = item_node("CPU Speed")
    picks = [o for o in option_strings(node) if o.strip() == want]
    if not picks:
        picks = [" " + want, want]
    for pick in picks:
        subprocess.check_call([
            "c64u", "config", "set", "U64 Specific Settings", "CPU Speed", pick])
        got = current_of(item_node("CPU Speed")).strip()
        if got == want:
            print("CPU Speed", want, flush=True)
            return
    fail("could not set CPU Speed %s from %s" % (want, json.dumps(node)[:300]))


def turbo_bytes():
    path = "/tmp/iec-turbo.bin"
    subprocess.check_call([
        "c64u", "machine", "read-mem", "D030", "--length", "2", "--raw", "-o", path])
    return open(path, "rb").read()


def wait_turbo_index(want, seconds=5):
    deadline = time.time() + seconds
    while time.time() < deadline:
        tb = turbo_bytes()
        if len(tb) >= 2 and (tb[1] & 0x0F) == want:
            return
        time.sleep(0.3)
    tb = turbo_bytes()
    got = tb[1] & 0x0F if len(tb) >= 2 else -1
    fail("turbo index want %d got %d (%s)" % (want, got, tb.hex()))


def row_bytes(scr, row):
    return bytes(scr[row * 40:(row + 1) * 40])


def screen():
    return drv.read_mem(drv.SCREEN, 1000)


def lines_addr():
    with open("build/whisper64.map") as handle:
        for line in handle:
            parts = line.split()
            if len(parts) >= 2 and parts[-1] == "lines":
                return int(parts[0], 16)
    fail("lines not in map")


def wait_row(row, needle, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if needle in row_bytes(screen(), row):
            return True
        time.sleep(0.8)
    return False


def wait_status(needles, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        row = row_bytes(screen(), 24)
        for needle in needles:
            if needle in row:
                return needle
        time.sleep(0.8)
    return None


def boot():
    drv.reset()
    drv.load_and_run(drv.PRG)
    if b"REU" in row_bytes(screen(), 24):
        drv.inject_keys([0x20], settle=0.8)


def selected_row(col):
    for r in range(4, 22):
        if (col[r * 40 + 1] & 0x0F) == 5:
            return r
    return -1


def load_named(name):
    before = turbo_bytes()
    drv.inject_keys([drv.K_F1], settle=0.6)
    if not wait_row(0, b"DIRECTORY", 30):
        fail("directory")
    target = name.encode("ascii")
    for _ in range(40):
        scr = screen()
        col = drv.read_mem(drv.COLOR, 1000)
        row = selected_row(col)
        if row >= 0 and target in row_bytes(scr, row):
            drv.inject_keys([drv.K_RETURN], settle=0.5)
            if not wait_row(24, b"LOADED", 180):
                fail("load " + name)
            deadline = time.time() + 30
            while time.time() < deadline:
                if b"444" in row_bytes(screen(), 24):
                    break
                time.sleep(0.3)
            time.sleep(3)
            after = turbo_bytes()
            print("load turbo", before.hex(), after.hex(), flush=True)
            if os.environ.get("W64_CHECK_TURBO") == "1" and before != after:
                fail("turbo register changed across load")
            return
        drv.inject_keys([drv.K_DOWN], settle=0.35)
    fail("find " + name)


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


def goto_line(n):
    codes = [K_CTRL_G] + [ord(ch) for ch in str(n)] + [drv.K_RETURN]
    drv.inject_keys(codes, settle=0.5)
    deadline = time.time() + 20
    while time.time() < deadline:
        scr = screen()
        if title_line(scr) == n and b"GOTO LINE" not in row_bytes(scr, 24):
            return
        time.sleep(0.2)
    fail("goto %d" % n)


def read_page(addr):
    rows = bytearray()
    row = 0
    while row < 48:
        off = addr + row * 80
        cell = bytes(drv.read_mem(off, 80))
        again = bytes(drv.read_mem(off, 80))
        if cell != again:
            time.sleep(0.15)
            again = bytes(drv.read_mem(off, 80))
        rows += again
        row += 1
    return bytes(rows)


def read_all(addr, n):
    got = []
    page = 0
    while len(got) < n:
        goto_line(page * 48 + 1)
        time.sleep(0.8)
        blob = read_page(addr)
        take = min(48, n - len(got))
        for row in range(take):
            raw = blob[row * 80:(row + 1) * 80].split(b"\x00", 1)[0]
            got.append(raw)
        page += 1
        if page > 20:
            fail("too many pages")
    return got


def oracle_lines(payload):
    parts = payload.split(b"\r")
    if parts and parts[-1] == b"":
        parts.pop()
    return parts


def boundary_report(payload):
    line = 1
    i = 0
    bad = 0
    while i < len(payload):
        if payload[i] == 13 and line % 48 == 0:
            print("boundary", line, "0D", flush=True)
        elif payload[i] == 0 and line % 48 == 0:
            print("boundary", line, "00", flush=True)
            bad += 1
        if payload[i] == 13:
            line += 1
        i += 1
    return bad


def compare_payload(label, got, want):
    print(label, "bytes", len(got), "want", len(want), flush=True)
    boundary_report(got)
    if got == want:
        print(label, "ok", flush=True)
        return
    gl = oracle_lines(got)
    wl = oracle_lines(want)
    shown = 0
    for n, (g, w) in enumerate(zip(gl, wl), 1):
        if g != w and shown < 8:
            print("line", n, "got", g[:70], "want", w[:70], flush=True)
            shown += 1
    if len(gl) != len(wl):
        print("line count", len(gl), len(wl), flush=True)
    fail(label + " mismatch")


def save_as(name, current_name):
    field = 9 + len(current_name) + 3
    before = turbo_bytes()
    drv.inject_keys([K_F2], settle=0.6)
    if not wait_row(24, b"SAVE AS", 15):
        fail("save prompt")
    drv.inject_keys([drv.K_DELETE] * 16, settle=0.5)
    row = row_bytes(screen(), 24)
    if row[field:field + len(current_name)].strip(b" \x00"):
        fail("name still in the edit field %r" % row[field:field + 16])
    drv.inject_keys(list(name), settle=0.4)
    row = row_bytes(screen(), 24)
    if row[field:field + len(name)] != name:
        fail("typed name missing %r" % row[field:field + 16])
    drv.inject_keys([drv.K_RETURN], settle=0.4)
    hit = wait_status([b"OVERWRITE", b"SAVED", b"SAVE ERROR", b"CANCELLED"], 180)
    if hit == b"OVERWRITE":
        drv.inject_keys([ord("Y")], settle=0.6)
        hit = wait_status([b"SAVED", b"SAVE ERROR"], 180)
    if hit != b"SAVED":
        fail("save result %s" % hit)
    time.sleep(2)
    after = turbo_bytes()
    print("save turbo", before.hex(), after.hex(), flush=True)
    if os.environ.get("W64_CHECK_TURBO") == "1" and before != after:
        fail("turbo register changed across save")


def download_image(local):
    letter, info = find_drive_8()
    remote = remote_of(info)
    subprocess.check_call(["c64u", "fs", "download", remote, local])
    return open(local, "rb").read()


def editor_up(name):
    row = row_bytes(screen(), 0)
    if name.encode("ascii")[:8] not in row:
        fail("editor left the screen %r" % row[:20])


def memory_matches(addr, want_lines):
    attempt = 0
    while attempt < 3:
        got = read_all(addr, len(want_lines))
        bad = None
        for n, (g, w) in enumerate(zip(got, want_lines), 1):
            if g != w:
                bad = (n, g, w)
                break
        if bad is None and len(got) == len(want_lines):
            print("memory ok", len(got), flush=True)
            return
        if attempt < 2:
            time.sleep(2)
        attempt += 1
    if bad:
        fail("memory line %d got %r want %r" % (bad[0], bad[1][:60], bad[2][:60]))
    fail("memory count %d" % len(got))


def load_three(addr, name, want_lines, speed):
    n = 0
    while n < 3:
        boot()
        set_speed(speed)
        if speed == "4":
            wait_turbo_index(3)
        load_named(name)
        memory_matches(addr, want_lines)
        n += 1
        print(name, "load", n, "ok", flush=True)


def suite(speed, backup, first, second, addr, want_lines, payload, warm):
    set_speed(speed)
    if warm:
        editor_up("DICEROLL")
    else:
        boot()
        load_named(backup)
        memory_matches(addr, want_lines)
    save_as(first, backup.encode("ascii"))
    time.sleep(3)
    image = download_image("/tmp/iec-after.d64")
    compare_payload(first.decode("ascii"), payload_named(image, first) or b"", payload)
    wrote = first.decode("ascii")
    time.sleep(3)
    save_as(first, wrote)
    time.sleep(3)
    image = download_image("/tmp/iec-after.d64")
    compare_payload(wrote + " overwrite",
                    payload_named(image, first) or b"", payload)
    time.sleep(3)
    save_as(second, wrote)
    time.sleep(3)
    image = download_image("/tmp/iec-after.d64")
    compare_payload(second.decode("ascii"), payload_named(image, second) or b"", payload)
    load_three(addr, first.decode("ascii"), want_lines, speed)
    print(speed, "MHz ok", flush=True)


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "both"
    if which not in ("both", "1", "4"):
        fail("usage: test_iec_quiet_c64u.py both|1|4")
    subprocess.check_call(["c64u", "info"])
    letter, info = find_drive_8()
    remote = remote_of(info)
    if not remote:
        fail("drive 8 has no image")
    ORIG["letter"] = letter
    ORIG["remote"] = remote
    ORIG["speed"] = current_of(item_node("CPU Speed"))
    ORIG["turbo"] = current_of(item_node("Turbo Control"))
    ORIG["touched"] = True
    print("recorded", ORIG["speed"], ORIG["turbo"], remote, flush=True)
    try:
        download_remote = remote
        image = " ".join(str(info.get(k) or "") for k in ("image_file", "image_path"))
        if "W64.d64" not in image:
            subprocess.check_call([
                "c64u", "drives", "mount", letter, "/SD/My Files/W64.d64",
                "--mode", "readwrite"])
            time.sleep(1)
            _, w64_info = find_drive_8()
            download_remote = remote_of(w64_info)
        subprocess.check_call(["c64u", "fs", "download", download_remote, "/tmp/iec-src.d64"])
        source = open("/tmp/iec-src.d64", "rb").read()
        payload = payload_named(source, b"DICEROLL-BACKUP")
        if payload is None:
            fail("DICEROLL-BACKUP missing")
        if b"\n" in payload or b"\x00" in payload or b"\r" not in payload:
            fail("oracle is not CR-only text")
        want_lines = oracle_lines(payload)
        if len(want_lines) != 444:
            fail("oracle lines %d" % len(want_lines))
        image = blank_d64()
        write_seq(image, b"DICEROLL-BACKUP", payload)
        back = payload_named(image, b"DICEROLL-BACKUP")
        if back != payload:
            fail("scratch image did not round-trip")
        open("/tmp/iec-quiet.d64", "wb").write(image)
        print("scratch image ok", len(payload), "bytes", flush=True)
        subprocess.check_call([
            "c64u", "drives", "mount-upload", letter, "/tmp/iec-quiet.d64",
            "--mode", "readwrite"])
        time.sleep(1)
        addr = lines_addr()
        if which in ("both", "1"):
            suite("1", "DICEROLL-BACKUP", b"SLOWA", b"SLOWB",
                  addr, want_lines, payload, False)
        if which in ("both", "4"):
            # The 4 MHz save starts from a buffer loaded at 1 MHz.
            set_speed("1")
            boot()
            load_named("DICEROLL-BACKUP")
            memory_matches(addr, want_lines)
            suite("4", "DICEROLL-BACKUP", b"Q2A", b"Q2B",
                  addr, want_lines, payload, True)
    finally:
        restore_machine()


if __name__ == "__main__":
    main()
