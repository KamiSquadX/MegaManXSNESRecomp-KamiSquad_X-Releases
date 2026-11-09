#!/usr/bin/env python3
"""Build KSX Zero's Special Move sheets and their Saber sidecar manifest.

    python tools/saber/build_ksx_zero_sheets.py \
        --xengine <Mega-Man-X-Engine checkout> --x8 <X8 16-bit project>

Sources (fan projects, credited in assets/ksx-zero/CREDITS.md):
  * Mega Man X Engine (Dragon-Developer and team, github.com/Dragon-Developer/
    Mega-Man-X-Engine): Ryuenjin, Hyouretsuzan, Raikousen, Mikazukizan
    (Kuuenbu) and Messenko (Rakuhouha). Body and saber layers are composited.
  * X8 16-bit by Alysson da Paz: Youdantotsu and Juuhazan (with its slash
    effect).

Every frame is placed on a 256x256 cell with Zero's position at (128,128),
the same anchor the Saber renderer uses (its 128x128 canvas centre). All
frames share one palette, quantized to at most 240 colours. Juuhazan's
purple blade is recoloured to the standard green so that Rekkyoudan (and the
X1 arms part) can turn every KSX saber purple the same way.

Writes assets/ksx-zero/sprites/*.png and tools/saber/ksx_zero_manifest.json;
build_saber.cmake converts them with convert_saber_zero.py. Only Pillow is
needed. Run it again only to change the art; its outputs are tracked.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "assets" / "ksx-zero" / "sprites"
MANIFEST = ROOT / "tools" / "saber" / "ksx_zero_manifest.json"
CELL = 256
ANCHOR = 128


def gm_sprite(root: Path, name: str):
    text = (root / "sprites" / name / f"{name}.yy").read_text(encoding="utf-8")
    data = json.loads(re.sub(r",(\s*[}\]])", r"\1", text))
    frames = [Image.open(root / "sprites" / name / f"{f['name']}.png").convert("RGBA")
              for f in data["frames"]]
    # Calibrated against the Zashiko Saber_1 stance: Zero stands at the
    # GameMaker origin + (1, -2).
    return frames, data["sequence"]["xorigin"] + 1, data["sequence"]["yorigin"] - 2


def gm_animation(root: Path, name: str):
    text = (root / "scripts" / "player_zero_animations" /
            "player_zero_animations.gml").read_text(encoding="utf-8")
    m = re.search(r'animation_add\("%s",\s*\[(.*?)\]' % re.escape(name), text, re.S)
    values = [int(v) for v in re.findall(r"-?\d+", m.group(1))]
    pairs = list(zip(values[0::2], values[1::2]))
    steps = []
    for (t0, frame), (t1, _) in zip(pairs, pairs[1:]):
        if t1 > t0:
            steps.append((frame, t1 - t0))
    last_t, last_frame = pairs[-1]
    if not steps or steps[-1][0] != last_frame:
        steps.append((last_frame, 1))
    return steps


def place(cell: Image.Image, image: Image.Image, zx: int, zy: int, dy: int = 0):
    cell.alpha_composite(image, (ANCHOR - zx, ANCHOR - zy + dy))


def recolor_orange_to_green(image: Image.Image) -> Image.Image:
    """The engine tints Mikazukizan's orange saber layer green in game."""
    image = image.copy()
    px = image.load()
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = px[x, y]
            if a and r > g >= b and r - b > 60:
                px[x, y] = (b, r, b, a)
    return image


# X8 16-bit's Zero reds and golds next to the Mega Man X Engine's: the
# renderer's Black Zero swap (mmx_saber_black_zero.c) knows only the latter.
X8_TO_ENGINE = {
    (240, 48, 0): (224, 32, 0),
    (239, 191, 0): (224, 192, 0),
    (240, 200, 24): (224, 192, 0),
    (176, 112, 0): (160, 96, 0),
    (107, 49, 24): (104, 32, 0),
    (124, 79, 0): (104, 64, 0),
}


def x8_engine_colors(image: Image.Image) -> Image.Image:
    image = image.copy()
    px = image.load()
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = px[x, y]
            if a and (r, g, b) in X8_TO_ENGINE:
                px[x, y] = X8_TO_ENGINE[(r, g, b)] + (a,)
    return image


def recolor_purple_to_green(image: Image.Image) -> Image.Image:
    px = image.load()
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = px[x, y]
            if a and b > 90 and b > g + 40 and r > g:
                low = min(g, 255)
                px[x, y] = (low, b, low, a)
    return image


def build(xengine: Path, x8: Path):
    moves = []  # (file stem, animation id, [(cell image, ticks)])

    def xe_move(stem, anim_id, body, saber, anim, green_blade=False):
        frames, zx, zy = gm_sprite(xengine, body)
        blades = gm_sprite(xengine, saber)[0] if saber else []
        if green_blade:
            blades = [recolor_orange_to_green(b) for b in blades]
        steps = gm_animation(xengine, anim)
        cells = {}
        out = []
        for frame, ticks in steps:
            if frame not in cells:
                cell = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
                place(cell, frames[min(frame, len(frames) - 1)], zx, zy)
                if blades:
                    place(cell, blades[min(frame, len(blades) - 1)], zx, zy)
                cells[frame] = cell
            out.append((frame, ticks))
        moves.append((stem, anim_id, cells, out))

    xe_move("ryuenjin", 0x41, "spr_zero_atk_ryuenjin", "spr_zero_saber_ryuenjin",
            "atk_ryuenjin")
    # The rise's end pose (frames 16-17) follows the same sheet.
    stem, anim_id, cells, steps = moves[-1]
    frames, zx, zy = gm_sprite(xengine, "spr_zero_atk_ryuenjin")
    blades = gm_sprite(xengine, "spr_zero_saber_ryuenjin")[0]
    for frame, ticks in gm_animation(xengine, "atk_ryuenjin_end|atk_ryuenjin"):
        if frame not in cells:
            cell = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
            place(cell, frames[min(frame, len(frames) - 1)], zx, zy)
            place(cell, blades[min(frame, len(blades) - 1)], zx, zy)
            cells[frame] = cell
        steps.append((frame, ticks))
    xe_move("hyouretsuzan", 0x42, "spr_zero_atk_hyouretsuzan",
            "spr_zero_saber_hyouretsuzan", "atk_hyouretsuzan")
    xe_move("hyouretsuzan_end", 0x43, "spr_zero_atk_hyouretsuzan",
            "spr_zero_saber_hyouretsuzan", "atk_hyouretsuzan_end|atk_hyouretsuzan")
    xe_move("raikousen", 0x44, "spr_zero_atk_raikousen", None, "atk_raikousen")
    xe_move("kuuenbu", 0x45, "spr_zero_atk_mikazukizan",
            "spr_zero_saber_mikazukizan", "atk_mikazukizan", green_blade=True)
    xe_move("rakuhouha", 0x46, "spr_zero_messenko", None, "messenko")
    # Raikousen started in the air uses the engine's air body with the ground
    # animation's timing (atk_raikousen_air shares atk_raikousen's frames).
    xe_move("raikousen_air", 0x49, "spr_zero_atk_raikousen_air", None,
            "atk_raikousen")

    # Rakuhouha's nine shards (obj_player_zero_messenko): one 4-frame loop per
    # launch angle 0, 22.5 ... 180 degrees, rotated about the sprite origin,
    # which lands on the cell anchor. Steps run angle-major: step 4*a + f.
    text = (xengine / "sprites" / "spr_zero_shot_messenko" /
            "spr_zero_shot_messenko.yy").read_text(encoding="utf-8")
    data = json.loads(re.sub(r",(\s*[}\]])", r"\1", text))
    shard_frames = [Image.open(xengine / "sprites" / "spr_zero_shot_messenko" /
                               f"{f['name']}.png").convert("RGBA")
                    for f in data["frames"]]
    ox, oy = data["sequence"]["xorigin"], data["sequence"]["yorigin"]
    cells, steps = {}, []
    for a in range(9):
        for f, image in enumerate(shard_frames[:4]):
            # Pad so the origin is the centre, then rotate about it.
            side = 2 * max(ox, oy, image.width - ox, image.height - oy)
            pad = Image.new("RGBA", (side, side), (0, 0, 0, 0))
            pad.alpha_composite(image, (side // 2 - ox, side // 2 - oy))
            rot = pad.rotate(22.5 * a, resample=Image.Resampling.NEAREST)
            cell = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
            place(cell, rot, side // 2, side // 2)
            cells[(a, f)] = cell
            steps.append(((a, f), 3))
    moves.append(("rakuhouha_shards", 0x4A, cells, steps))

    sheets = x8 / "Zero_mod" / "Sprites" / "BossAbilities"

    def x8_move(stem, anim_id, sheet, size, fps, cell_seq, effect=None,
                effect_seq=None, recolor=False):
        image = Image.open(sheets / sheet).convert("RGBA")
        fx = Image.open(sheets / effect).convert("RGBA") if effect else None
        # Godot centres the frame; Zero stands at the centre - (1, 2).
        zx, zy = size // 2 - 1, size // 2 - 2
        cells = {}
        steps = []
        for i, index in enumerate(cell_seq):
            key = (index, effect_seq[i] if effect_seq else None)
            if key not in cells:
                frame = x8_engine_colors(
                    image.crop((index * size, 0, index * size + size, size)))
                if recolor:
                    frame = recolor_purple_to_green(frame)
                cell = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
                place(cell, frame, zx, zy)
                if fx is not None and effect_seq[i] is not None:
                    e = effect_seq[i]
                    place(cell, fx.crop((e * size, 0, e * size + size, size)), zx, zy, -3)
                cells[key] = cell
            ticks = round((i + 1) * 60 / fps) - round(i * 60 / fps)
            steps.append((key, ticks))
        moves.append((stem, anim_id, cells, steps))

    x8_move("youdantotsu", 0x47, "Youdantotsu.png", 256, 23,
            [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 14])
    juuhazan = [0, 1, 1, 1, 1, 2, 3, 4, 5, 6, 7, 8] + [8] * 13
    effect = [None, 7, 8, None, 1, 2, 3, 4, 5, 6] + [None] * 15
    x8_move("juuhazan", 0x48, "Juuhazan.png", 192, 30, juuhazan,
            "Juuhazan_effect.png", effect, recolor=True)
    return moves


def quantize(moves):
    """One shared palette for every cell (<= 240 colours, binary alpha)."""
    cells = [cell for _, _, c, _ in moves for cell in c.values()]
    atlas = Image.new("RGB", (CELL, CELL * len(cells)), (0, 0, 0))
    mask = Image.new("L", atlas.size, 0)
    for i, cell in enumerate(cells):
        alpha = cell.getchannel("A").point(lambda a: 255 if a >= 128 else 0)
        atlas.paste(cell.convert("RGB"), (0, i * CELL))
        mask.paste(alpha, (0, i * CELL))
    # Quantize only opaque colours: fill transparency with an opaque pixel's
    # colour so it adds no palette entry of its own.
    opaque = atlas.copy()
    quant = opaque.quantize(colors=240, method=Image.Quantize.MEDIANCUT,
                            dither=Image.Dither.NONE)
    rgb = quant.convert("RGB")
    i = 0
    for stem, anim_id, cdict, steps in moves:
        for key in list(cdict):
            box = (0, i * CELL, CELL, (i + 1) * CELL)
            out = rgb.crop(box).convert("RGBA")
            out.putalpha(mask.crop(box))
            cdict[key] = out
            i += 1


def write(moves):
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    manifest = {"format": "mmx-saber-manifest", "version": 1,
                "alpha_threshold": 128, "sources": [], "frames": [],
                "animations": []}
    frame_id = 4001
    for source_id, (stem, anim_id, cdict, steps) in enumerate(moves, start=1):
        keys = list(cdict)
        sheet = Image.new("RGBA", (CELL * len(keys), CELL), (0, 0, 0, 0))
        ids = {}
        for n, key in enumerate(keys):
            cell = cdict[key]
            sheet.alpha_composite(cell, (n * CELL, 0))
            bbox = cell.getbbox()
            if not bbox:
                bbox = (ANCHOR, ANCHOR, ANCHOR + 1, ANCHOR + 1)
            x0, y0, x1, y1 = bbox
            ids[key] = frame_id
            manifest["frames"].append({
                "id": frame_id, "source": source_id, "source_frame": n,
                "body": {"rect": [n * CELL + x0, y0, x1 - x0, y1 - y0],
                         "origin": [x0 - (ANCHOR - 64), y0 - (ANCHOR - 64)]},
            })
            frame_id += 1
        path = OUT_DIR / f"{stem}.png"
        # A pixel left fully transparent must stay RGB (0,0,0) so the file is
        # reproducible.
        sheet.save(path, optimize=True)
        manifest["sources"].append({
            "id": source_id, "file": f"{stem}.png",
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        manifest["animations"].append({
            "id": anim_id, "facing_xor": 1,
            "steps": [{"frame": ids[key], "ticks": ticks} for key, ticks in steps]})
    MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--xengine", type=Path, required=True)
    parser.add_argument("--x8", type=Path, required=True)
    args = parser.parse_args()
    moves = build(args.xengine, args.x8)
    quantize(moves)
    write(moves)
    print(f"wrote {len(moves)} sheets to {OUT_DIR} and {MANIFEST}")


if __name__ == "__main__":
    main()
