#!/usr/bin/env python3
"""Insert the Sub Tank pickup hooks for Xtreme Sub Tanks (X-tensions: X2 game
extension) into regenerated X1 and X2 code.

A Sub Tank pickup looks for a slot without bit 7 and, when all four are
owned, falls through to $DC15 (X2) / $81:E662 (X1) without taking the tank
(no chime, no pickup flag). With the X2 game extension both games' tanks
count together, up to eight: there the hook records the pickup
(src/x2ext/mmx_x2share.c MmxX2ShareTankPickup) and continues on the success
path ($DC1A / $81:E667: the chime, the refill), as a free slot would.
Also OPTION MODE's DOUBLE TAP DASH on X2: X2 keeps X1's own second-tap
dash ($08:BB06, LDA $53; BIT $3B, X1 $81:9742); the hook clears the armed
direction there (MmxOptionsX2TapHook), as X1's options hook does, so only
OPTION MODE's double tap dashes.
Idempotent: marked lines are removed before inserting again.

    python tools/apply_xtreme_tank_hooks.py src/gen x2/src/gen
"""
import argparse
from pathlib import Path

MARKER = '/*MMX-XTREME-TANK*/'
# (trace PC of the fall-through block, label of the success block, X2?)
SITES = [
    ('0x81E662', 'L_E667_M1X1', 0),
    ('0x01DC15', 'L_DC1A_M1X1', 1),
]


# Block PC -> call after the block's cycle bookkeeping.
AFTER_ENTRY = {
    '0x08BB06': 'extern void MmxOptionsX2TapHook(CpuState *); MmxOptionsX2TapHook(cpu);',
}


def apply(text):
    lines = [line for line in text.splitlines(keepends=True) if MARKER not in line]
    out, armed, found = [], None, set()
    for line in lines:
        out.append(line)
        for pc, label, x2 in SITES:
            if f'cpu_trace_block(cpu, {pc});' in line:
                armed = (pc, label, x2)
        for pc, call in AFTER_ENTRY.items():
            if f'cpu_trace_block(cpu, {pc});' in line:
                armed = (pc, call, None)
        if armed and armed[2] is None and 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            out.append(f'    {MARKER} {{ {armed[1]} }}' + chr(10))
            found.add(armed[0])
            armed = None
            continue
        if armed and 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            pc, label, x2 = armed
            out.append(f'    {MARKER} {{ extern int MmxX2ShareTankPickup(CpuState *, int); '
                       f'if (MmxX2ShareTankPickup(cpu, {x2})) goto {label}; }}\n')
            found.add(pc)
            armed = None
    return ''.join(out), found


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('gen_dirs', nargs='+')
    args = parser.parse_args()
    found = set()
    for gen in args.gen_dirs:
        for path in sorted(Path(gen).glob('bank*_v2.c')):
            text = path.read_text(encoding='utf-8')
            new, hit = apply(text)
            if hit or MARKER in text:
                path.write_text(new, encoding='utf-8')
            found |= hit
    missing = ({pc for pc, _, _ in SITES} | set(AFTER_ENTRY)) - found
    for pc in sorted(found):
        print(f'hooked {pc}')
    if missing:
        raise SystemExit(f'not found: {", ".join(sorted(missing))}')


if __name__ == '__main__':
    main()
