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
X2 co-op (src/x2ext/mmx_x2coop.c):
- Unified camera: the player's X / Y reads in $00:DE9E / $00:DEBD's blocks
  ($00:DE9E, $00:DEA9, $00:DEBD, $00:DEC8) return both seats' midpoint
  (MmxX2CoopCameraAxis), as X1's co-op camera does.
- $08:C9B8 uploads the live body's pose tiles to X's VRAM; on Zero's seat's
  pass it is skipped (PHP; then the PLP/RTL before it, L_C9B6), so Zero's
  frames never replace X's.
Escape Bugs (src/mmx_escape.c): the pause menu's escape check (X1 $00:C93C,
X2 $00:C622, both "LDA stage; ...; AND #$40; RTS") answers from its RTS
(X1 $C94E, X2 $C641) while ESCAPE.U's held Start or B decides
(MmxEscapeCheckAnswer: 1 available, A = $40; 2 not, A = 0).
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


# Block PC -> (address read, axis): the camera's reads of the player.
CAMERA_READS = {
    '0x00DE9E': ('0x09dd', 5), '0x00DEA9': ('0x09dd', 5),
    '0x00DEBD': ('0x09e0', 8), '0x00DEC8': ('0x09e0', 8),
}
CHR_ENTRY = '0x08C9B8'
# Heart Tanks past eight (src/x2ext/mmx_x2share.c MmxX2ShareHeartRoom): the
# pickup block ($81:EAEA X1, $01:E477 X2: LDA $0B; TSB hearts; LDA max HP;
# CMP #$20; BCC) sets the flag and goes the full-pickup way (X1 $EAFB, X2
# $E488) when there is room for blue health.
HEART_PICKUPS = {'0x81EAEA': ('0x1f9c', 'L_EAFB_M1X1', 0), '0x01E477': ('0x1fd3', 'L_E488_M1X1', 1)}
# X2 co-op (src/x2ext/mmx_x2coop.c): $08:D865 (objects testing the player)
# runs interpreted while a second seat could answer, so its entry and returns
# are hooked (the object loops that end a seat kept in for one object run
# interpreted already: interpreter hooks in mmx_x2coop.c); at
# $00:E0EA (the main seat below the camera's bottom, about to die) the shared
# camera is not a pit.
# $08:CC7F (the solid-object routine: touch test, push out, ride flags) and
# $08:D651 (touching: the player's $0A04 carry bits) run interpreted the same
# way, so both seats get a turn at them; $08:CD2E (a solid object's rider
# carry) runs interpreted on frames that noted riders, to carry each seat on
# the object's top and no other.
OVERLAP_FUNCTIONS = {'bank_08_D865_': ('MmxX2CoopOverlapInterp', '0x08d865u', 'overlap'),
                     'bank_08_CC7F_': ('MmxX2CoopSolidInterp', '0x08cc7fu', 'solid'),
                     'bank_08_D651_': ('MmxX2CoopSolidInterp', '0x08d651u', 'touching'),
                     'bank_08_CD2E_': ('MmxX2CoopCarryInterp', '0x08cd2eu', 'carry'),
                     'bank_07_E22A_': ('MmxX2CoopSolidInterp', '0x07e22au', 'lava')}
OBJECT_DONE = []
BELOW_CHECK = '0x00E0EA'
ESCAPE_CHECKS = {'0x00C93C': 'L_C94E_M1X1', '0x00C622': 'L_C641_M1X1'}
# X2 co-op: the beam-in's end resets the camera's top limit from the
# checkpoint ($08:A628 JSL $00:E67B); a co-op arrival skips it.
BEAM_CAMERA = {'0x08A628': 'L_A62C_M1X1'}
# X2's weapon switch ($08:BED3): none for Zero (to its RTS, $08:BECB).
WEAPON_SWITCH = {'0x08BED3': 'L_BECB_M1X1'}

# Block PC -> call after the block's cycle bookkeeping.
AFTER_ENTRY = {
    '0x08BB06': 'extern void MmxOptionsX2TapHook(CpuState *); MmxOptionsX2TapHook(cpu);',
    # X2 co-op: health / Heart Tank states after the touch (the held seat).
    '0x01D62A': 'extern void MmxX2CoopHeldState(CpuState *); MmxX2CoopHeldState(cpu);',
    '0x01D65F': 'extern void MmxX2CoopHeldState(CpuState *); MmxX2CoopHeldState(cpu);',
    '0x01E4BD': 'extern void MmxX2CoopHeldState(CpuState *); MmxX2CoopHeldState(cpu);',
    '0x01E4CA': 'extern void MmxX2CoopHeldState(CpuState *); MmxX2CoopHeldState(cpu);',
    '0x01E502': 'extern void MmxX2CoopHeldState(CpuState *); MmxX2CoopHeldState(cpu);',
    # X2 co-op: Wheel Gator's oil holds the other seat up too (its RTL).
    '0x058654': 'extern void MmxX2CoopOilFloor(CpuState *); MmxX2CoopOilFloor(cpu);',
}


def apply(text):
    lines = [line for line in text.splitlines(keepends=True) if MARKER not in line]
    out, armed, found = [], None, set()
    import re
    camera_block = None
    overlap_fn = None
    for line in lines:
        for name in OVERLAP_FUNCTIONS:
            if line.startswith('RecompReturn ' + name) and line.rstrip().endswith('{'):
                overlap_fn = name
        out.append(line)
        if overlap_fn and 'g_cpu_entry_s[g_recomp_stack_top - 1] = _entry_s;' in line:
            check, pc, tag = OVERLAP_FUNCTIONS[overlap_fn]
            out.append(f'  {MARKER} {{ extern int {check}(void); if ({check}()) {{ '
                       f'RecompReturn _r = interp_tier_dispatch_bank_miss(cpu, {pc}, _entry_s, _hrv); '
                       f'RecompStackPop(); return _r; }} }}' + chr(10))
            found.add(tag)
            overlap_fn = None
        for pc in OBJECT_DONE:
            if f'cpu_trace_block(cpu, {pc});' in line:
                out.append(f'    {MARKER} {{ extern void MmxX2CoopObjectDone(void); MmxX2CoopObjectDone(); }}' + chr(10))
                found.add(pc)
        if f'cpu_trace_block(cpu, {BELOW_CHECK});' in line:
            m = re.search(r'L_E0EA_(M\dX\d)', ''.join(out[-4:]))
            if m:
                out.append(f'    {MARKER} {{ extern int MmxX2CoopMainBelowOk(void); '
                           f'if (MmxX2CoopMainBelowOk()) goto L_E103_{m[1]}; }}' + chr(10))
                found.add(BELOW_CHECK)
        for pc in CAMERA_READS:
            if f'cpu_trace_block(cpu, {pc});' in line:
                camera_block = pc
        if camera_block:
            addr, axis = CAMERA_READS[camera_block]
            m = re.search(r'uint16 (_v\d+) = cpu_read16\(cpu, cpu->DB, \(uint16\)\(' + addr + r'\)\);', line)
            if m:
                out.append(f'    {MARKER} {{ extern unsigned MmxX2CoopCameraAxis(unsigned, unsigned); '
                           f'{m[1]} = (uint16)MmxX2CoopCameraAxis({m[1]}, {axis}); }}' + chr(10))
                found.add(camera_block)
                camera_block = None
        for pc, (flags, label, x2) in HEART_PICKUPS.items():
            if f'cpu_trace_block(cpu, {pc});' in line:
                armed = (pc, (flags, label, x2), 'heart')
        if armed and armed[2] == 'heart' and 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            flags, label, x2 = armed[1]
            out.append(f'    {MARKER} {{ extern int MmxX2ShareHeartRoom(int); if (MmxX2ShareHeartRoom({x2})) {{ '
                       f'const uint8 _b = cpu_read8(cpu, 0x00, (uint16)(cpu->D + 0x0b)); '
                       f'cpu_write8(cpu, cpu->DB, (uint16)({flags}), (uint8)(cpu_read8(cpu, cpu->DB, (uint16)({flags})) | _b)); '
                       f'goto {label}; }} }}' + chr(10))
            found.add(armed[0])
            armed = None
            continue
        for pc, label in ESCAPE_CHECKS.items():
            if f'cpu_trace_block(cpu, {pc});' in line:
                armed = (pc, label, 'escape')
        for pc, label in BEAM_CAMERA.items():
            if f'cpu_trace_block(cpu, {pc});' in line:
                armed = (pc, label, 'beamcam')
        for pc, label in WEAPON_SWITCH.items():
            if f'cpu_trace_block(cpu, {pc});' in line:
                armed = (pc, label, 'wswitch')
        if armed and armed[2] == 'wswitch' and 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            out.append(f'    {MARKER} {{ extern int MmxX2ZeroSkipWeaponSwitch(void); '
                       f'if (MmxX2ZeroSkipWeaponSwitch()) goto {armed[1]}; }}' + chr(10))
            found.add(armed[0])
            armed = None
        if armed and armed[2] == 'beamcam' and 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            out.append(f'    {MARKER} {{ extern int MmxX2CoopBeamCameraSkip(void); '
                       f'if (MmxX2CoopBeamCameraSkip()) goto {armed[1]}; }}' + chr(10))
            found.add(armed[0])
            armed = None
        if armed and armed[2] == 'escape' and 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            out.append(f'    {MARKER} {{ extern int MmxEscapeCheckAnswer(void); const int _ea = MmxEscapeCheckAnswer(); '
                       f'if (_ea) {{ const uint8 _a = _ea == 1 ? 0x40 : 0; '
                       f'cpu->A = (uint16)((cpu->A & 0xff00) | _a); cpu->_flag_Z = _a == 0; cpu->_flag_N = 0; '
                       f'cpu->P = (uint8)((cpu->P & ~0x82) | (_a ? 0 : 0x02)); goto {armed[1]}; }} }}' + chr(10))
            found.add(armed[0])
            armed = None
            continue
        if f'cpu_trace_block(cpu, {CHR_ENTRY});' in line:
            armed = (CHR_ENTRY, None, 'chr')
        for pc, label, x2 in SITES:
            if f'cpu_trace_block(cpu, {pc});' in line:
                armed = (pc, label, x2)
        for pc, call in AFTER_ENTRY.items():
            if f'cpu_trace_block(cpu, {pc});' in line:
                armed = (pc, call, None)
        if armed and armed[2] == 'chr' and 'cpu->coprocessor_master_cycles = cpu->master_cycles;' in line:
            out.append(f'    {MARKER} {{ extern int MmxX2CoopSkipChr(void); if (MmxX2CoopSkipChr()) {{ '
                       f'cpu_mirrors_to_p(cpu); cpu_write8(cpu, 0x00, cpu->S, cpu->P); '
                       f'cpu->S = (uint16)(cpu->S - 1); goto L_C9B6_M1X1; }} }}' + chr(10))
            found.add(armed[0])
            armed = None
            continue
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
    missing = ({pc for pc, _, _ in SITES} | set(AFTER_ENTRY) | set(CAMERA_READS) | {CHR_ENTRY} | set(ESCAPE_CHECKS) | set(BEAM_CAMERA) | set(WEAPON_SWITCH) | set(HEART_PICKUPS) | set(OBJECT_DONE) | {BELOW_CHECK, 'overlap', 'solid', 'touching', 'carry', 'lava'}) - found
    for pc in sorted(found):
        print(f'hooked {pc}')
    if missing:
        raise SystemExit(f'not found: {", ".join(sorted(missing))}')


if __name__ == '__main__':
    main()
