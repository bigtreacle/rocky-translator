#!/usr/bin/env python3
"""Validate data/phrases.json before uploading it to the device.

    python3 tools/check_phrases.py [path]

Errors (exit 1): bad JSON, missing fields, unknown trigger, bad note or
duration, duplicate id, `next` pointing nowhere. Warnings: notes outside the
C4..C6 voicing the speaker likes (the firmware still plays them, clamped by
octave), more than 8 chords, long text.
"""
import json
import sys

TRIGGERS = {"tap", "heard_speech", "heard_whistle", "picked_up", "shaken", "idle"}
MAX_CHORDS = 8


def main(path):
    try:
        with open(path) as f:
            doc = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        print(f"error: {path}: {e}")
        return 1
    errors, warnings = [], []
    phrases = doc.get("phrases")
    if not isinstance(phrases, list) or not phrases:
        print("error: no 'phrases' array")
        return 1
    ids = {}
    for i, p in enumerate(phrases):
        where = f"phrase {i} ({p.get('id', '?')})"
        for key in ("id", "trigger", "chords", "text"):
            if key not in p:
                errors.append(f"{where}: missing '{key}'")
        pid = p.get("id")
        if pid in ids:
            errors.append(f"{where}: duplicate id")
        ids[pid] = p
        if p.get("trigger") not in TRIGGERS:
            errors.append(f"{where}: unknown trigger {p.get('trigger')!r}")
        chords = p.get("chords") or []
        if not chords:
            errors.append(f"{where}: no chords")
        if len(chords) > MAX_CHORDS:
            warnings.append(f"{where}: {len(chords)} chords, firmware keeps the first {MAX_CHORDS}")
        for j, c in enumerate(chords):
            notes = c.get("notes", [])
            if not isinstance(notes, list) or len(notes) > 4:
                errors.append(f"{where} chord {j}: 'notes' must be a list of up to 4 MIDI numbers")
                continue
            for n in notes:
                if not isinstance(n, int) or not 0 <= n <= 127:
                    errors.append(f"{where} chord {j}: bad note {n!r}")
                elif n and not 60 <= n <= 84:
                    warnings.append(f"{where} chord {j}: note {n} outside C4..C6")
            ms = c.get("ms")
            if not isinstance(ms, int) or not 30 <= ms <= 5000:
                errors.append(f"{where} chord {j}: 'ms' must be 30..5000")
            gap = c.get("gap_ms", 0)
            if not isinstance(gap, int) or not 0 <= gap <= 5000:
                errors.append(f"{where} chord {j}: 'gap_ms' must be 0..5000")
            vel = c.get("velocity", 100)
            if not isinstance(vel, int) or not 1 <= vel <= 127:
                errors.append(f"{where} chord {j}: 'velocity' must be 1..127")
        if len(p.get("text", "")) > 200:
            warnings.append(f"{where}: text over 200 characters will scroll a lot")
    for pid, p in ids.items():
        nxt = p.get("next")
        if nxt and nxt not in ids:
            errors.append(f"phrase {pid}: next={nxt!r} does not exist")
    for w in warnings:
        print("warning:", w)
    for e in errors:
        print("error:", e)
    by_trigger = {}
    for p in phrases:
        by_trigger[p.get("trigger")] = by_trigger.get(p.get("trigger"), 0) + 1
    print(f"{len(phrases)} phrases:", ", ".join(f"{k} {v}" for k, v in sorted(by_trigger.items())))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "data/phrases.json"))
