#!/usr/bin/env python3
"""Sting Chameleon's fallen rocks against Zero's saber, paired with LLE hooks.

$83:BD6A (object $18, state 6) walks the player shots and breaks the rock
tiles a shot's hit area touches. $83:BD6F starts one shot's turn and
$83:BE25 ends it (the saber takes one turn per tile its hit area covers). Generated blocks call MmxSaberRockHook on entry; the
interpreter hooks in mmx_zero_plugin.c cover the interpreted paths."""
import argparse
from pathlib import Path
import re

MARKER = '/*MMX-SABER-ROCK*/'
PCS = ('83BD6F', '83BE25')


def apply(text):
    text = ''.join(line for line in text.splitlines(keepends=True) if MARKER not in line)

    def inject(match):
        return match[0] + (f'    {MARKER} {{ extern void MmxSaberRockHook(CpuState *, uint32_t); '
                           f'MmxSaberRockHook(cpu, 0x{match["pc"]}); }}\n')
    return re.subn(r'    cpu_trace_block\(cpu, 0x(?P<pc>' + '|'.join(PCS) + r')\);\n'
                   r'(?:(?!cpu_trace_block)[\s\S])*?'
                   r'    cpu->coprocessor_master_cycles = cpu->master_cycles;\n', inject, text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gen-dir', type=Path, required=True)
    args = parser.parse_args()
    count = 0
    pattern = re.compile(r'cpu_trace_block\(cpu, 0x(?:' + '|'.join(PCS) + r')\)')
    for path in args.gen_dir.glob('*.c'):
        original = path.read_text(encoding='utf-8')
        if not pattern.search(original):
            continue
        updated, sites = apply(original)
        count += sites
        if updated != original:
            path.write_text(updated, encoding='utf-8', newline='\n')
    print(f'MMX saber rocks: {count} native hooks; interpreter hooks cover other paths')


if __name__ == '__main__':
    main()
