#!/usr/bin/env python3
"""Insert Zero's X2 hooks into regenerated X2 code (X-tensions: X2 game).

The X2 counterparts of tools/apply_zero_hooks.py's X1 sites, found by
aligning X1's and X2's player code (docs/x2-zero-port.md). Each call goes to
src/x2ext/mmx_x2zero.c, which runs Zero's X1 code on an X1-layout view of
X2's RAM. Idempotent: marked lines are removed before inserting again.
"""
import argparse
from pathlib import Path
import re

MARKER = '/*MMX-X2-ZERO*/'
# Block PC -> call inserted after the block's cycle bookkeeping.
AFTER_ENTRY = {
    0x089479: 'extern void MmxX2ZeroPrePlayer(void); MmxX2ZeroPrePlayer();',
    0x08947E: 'extern void MmxX2ZeroPlayerEnd(void); MmxX2ZeroPlayerEnd();',
    0x08C690: 'extern void MmxX2ZeroAnimationStart(unsigned, unsigned); MmxX2ZeroAnimationStart(cpu->D, cpu->A & 255);',
    0x08C673: 'extern void MmxX2ZeroAnimationAdvance(unsigned); MmxX2ZeroAnimationAdvance(cpu->D);',
}
HITBOX = 0x08D86D  # X1 $84:9C16: a player shot's hitbox pointer, +$20,X
# X1 $84:9E15..$84:9E73: a player shot touching an enemy. X2 keeps the
# projectile at $1F30, its reaction at $1F2F and the enemy table at $F3A4.
CONTACT = 0x08DA9B
CONTACT_END = 0x08DAFD   # the damage block; the response read comes before it
# Co-op (src/x2ext/mmx_x2coop.c): the player routine's end, before its
# PLD/PLP/RTL at $08:94C3 (last store: STA $11). Player 2's pass restarts the
# routine at its entry after undoing the prologue's pushes.
PASS_END = 0x0894AD
# Co-op: the player's terrain update $08:C314 ends at its PLD ($08:C325).
TERRAIN_END = 0x08C325
# Co-op: enemy contact with the player, $08:D752 (both generated variants):
# its entry and every RTL, where player 2's turn restarts it.
PLAYER_CONTACT = 0x08D752
REQUIRED = set(AFTER_ENTRY) | {HITBOX, CONTACT, 0x08DAB2, CONTACT_END, PASS_END, TERRAIN_END,
                               PLAYER_CONTACT}


def apply(text):
    lines = [line for line in text.splitlines(keepends=True) if MARKER not in line]
    output, found, pc = [], set(), 0
    contact_fn = None
    for line in lines:
        fn = re.match(r'RecompReturn bank_08_D752_M1X([01])\(CpuState \*cpu\) \{', line)
        if fn:
            contact_fn = fn[1]
        elif line.startswith('RecompReturn '):
            contact_fn = None
        if contact_fn is not None and 'RTL pop hardware return frame' in line:
            output.append(f'    {MARKER} {{ extern int MmxX2CoopContactEnd(CpuState *); '
                          f'if (MmxX2CoopContactEnd(cpu)) goto L_D752_M1X{contact_fn}; }}' + chr(10))
        block = re.search(r'cpu_trace_block\(cpu, 0x([0-9A-Fa-f]+)\);', line)
        if block:
            pc = int(block[1], 16)
        output.append(line)
        if pc in AFTER_ENTRY and 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            output.append(f'    {MARKER} {{ {AFTER_ENTRY[pc]} }}\n')
            found.add(pc)
        if pc == PASS_END and 'cpu_write8(cpu, 0x00, (uint16)(cpu->D + 0x0011)' in line:
            output.append(f'    {MARKER} {{ extern int MmxX2CoopPassEnd(CpuState *); '
                          f'if (MmxX2CoopPassEnd(cpu)) goto L_9428_M1X1; }}\n')
            found.add(pc)
        if contact_fn is not None and pc == PLAYER_CONTACT and                 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            output.append(f'    {MARKER} {{ extern void MmxX2CoopContactBegin(CpuState *); '
                          f'MmxX2CoopContactBegin(cpu); }}' + chr(10))
            found.add(pc)
        if pc == TERRAIN_END and 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            output.append(f'    {MARKER} {{ extern int MmxX2CoopTerrainEnd(CpuState *); '
                          f'if (MmxX2CoopTerrainEnd(cpu)) goto L_C314_M1X1; }}' + chr(10))
            found.add(pc)
        if pc == HITBOX and '0x0020' in line and 'cpu->X' in line:
            load = re.search(r'uint16 (_v\d+) = cpu_read16\(cpu,', line)
            if load:
                output.append(f'    {MARKER} {{ extern unsigned MmxX2ZeroHitbox(unsigned, unsigned, unsigned); '
                              f'{load[1]} = (uint16)MmxX2ZeroHitbox(cpu->D, cpu->X, {load[1]}); }}\n')
                found.add(pc)
        if pc == CONTACT and '0x000a' in line and 'cpu->X' in line:
            load = re.search(r'uint16 (_v\d+) = cpu_read16\(cpu,', line)
            if load:
                output.append(f'    {MARKER} {{ extern unsigned MmxX2ZeroContactClass(unsigned, unsigned, unsigned, bool); '
                              f'{load[1]} = (uint16)MmxX2ZeroContactClass(cpu->D, cpu->X, {load[1]}, false); }}\n')
                found.add(pc)
        if CONTACT < pc < CONTACT_END and 'cpu_write8' in line and '(0x1f2f)' in line:
            output.append(f'    {MARKER} {{ extern uint8_t g_ram[0x20000]; extern unsigned MmxX2ZeroContactClass(unsigned, unsigned, unsigned, bool); '
                          f'cpu_write8(cpu, cpu->DB, 0x1f2f, (uint8)MmxX2ZeroContactClass(cpu->D, cpu->X, g_ram[0x1f2f], true)); }}\n')
            found.add(0x08DAB2)
        if CONTACT < pc < CONTACT_END and 'uint8 ' in line and '0xf3a4' in line:
            load = re.search(r'uint8 (_v\d+) = cpu_read8', line)
            if load:
                output.append(f'    {MARKER} {{ extern unsigned MmxX2ZeroResponse(unsigned, unsigned, unsigned); '
                              f'{load[1]} = (uint8)MmxX2ZeroResponse(cpu->D, cpu->X, {load[1]}); }}\n')
        if pc == CONTACT_END and 'uint8 ' in line and '0xf3a4' in line:
            load = re.search(r'uint8 (_v\d+) = cpu_read8', line)
            if load:
                output.append(f'    {MARKER} {{ extern unsigned MmxX2ZeroDamage(unsigned, unsigned, unsigned); '
                              f'{load[1]} = (uint8)MmxX2ZeroDamage(cpu->D, cpu->X, {load[1]}); }}\n')
                found.add(pc)
    return ''.join(output), found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gen-dir', type=Path, required=True)
    args = parser.parse_args()
    found = set()
    for path in sorted(args.gen_dir.glob('bank*_v2.c')):
        text = path.read_text(encoding='utf-8')
        if not any(f'0x{pc:06X}' in text for pc in REQUIRED):
            continue
        updated, sites = apply(text)
        found |= sites
        if updated != text:
            path.write_text(updated, encoding='utf-8', newline='\n')
    missing = REQUIRED - found
    if missing:
        raise SystemExit('apply_x2_zero_hooks: missing ' +
                         ', '.join(f'${pc:06X}' for pc in sorted(missing)))
    print(f'apply_x2_zero_hooks: {len(found)} site(s)')


if __name__ == '__main__':
    main()
