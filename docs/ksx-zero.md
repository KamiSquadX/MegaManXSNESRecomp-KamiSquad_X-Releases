# KSX Zero

**Zero behavior → KSX Zero** (Add Zero, X / Zero Co-op and X / Zero / Axl
Co-op). Saber Zero by RaphaelAzev is the base: the Y saber, its combos, the
X1 armor parts and Black Zero all work the same. Tuning lives in the
**KSX Zero settings** mod (by KamiSquad_X; with RaphaelAzev as the base),
listed under Saber Zero settings.

## Buttons

| Button | Action |
| --- | --- |
| Y | Saber (unchanged) |
| R2 (ARMOR ABILITY) | Buster, with its charge shots (Rakuhouha when it is selected) |
| X (SHOT 2) | Special Move |
| L2 (GIGA ATTACK) | Giga Attack |
| L / R | Switch between the buster and Rakuhouha |

GIGA ATTACK is a new KEY CONFIG row, on L2 by default.

## Skills

Zero learns a skill by defeating its Maverick: the skill counts as learned
whenever that Maverick's weapon is owned, so passwords carry it too. KSX Zero
never uses special weapons, including the X2 and X3 weapon packs; the pause
menu lists his skills where X's weapons would be. Every skill cancels the
three ground slashes at any point.

| Input | Skill | Learned from | Weak |
| --- | --- | --- | --- |
| Up + X | Ryuenjin: rising fire uppercut, multi-hit; tap for a hop, hold to rise higher; once per jump, whether started on the ground or in the air (again after landing or a wall) | Flame Mammoth | Chill Penguin |
| L2 | Rakuhouha: a blast around Zero, then nine shards that each hit on their own; Zero is untouchable for all of it | Sting Chameleon | Storm Eagle |
| Dash + X (or Dash held + X in the air) | Raikousen: lightning dash stab, untouchable while dashing | Spark Mandrill | Armored Armadillo |
| X in the air | Kuuenbu: one somersault slash per press | Armored Armadillo | Launch Octopus |
| Down + X in the air (hold) | Hyouretsuzan: ice stab down until landing | Chill Penguin | Spark Mandrill |
| Forward + X | Youdantotsu: lunging stab | Launch Octopus | Boomer Kuwanger |
| X standing still | Juuhazan: overhead swing | Storm Eagle | Flame Mammoth |
| Passive | Rekkyoudan: purple saber | Boomer Kuwanger | Sting Chameleon |

Against the Maverick weak to its Maverick's weapon, a skill hits with that
weapon's X1 contact class (the table in `mmx_weapon_weakness.inc`), so the
boss takes the weapon's reaction and at least its weakness damage: Ryuenjin
burns Chill Penguin, Juuhazan hurts Flame Mammoth, Youdantotsu Boomer
Kuwanger, Hyouretsuzan freezes Spark Mandrill, Rakuhouha hurts Storm Eagle,
Raikousen shocks Armored Armadillo and Kuuenbu hurts Launch Octopus.
Each skill has normal damage, boss damage and hit-priority settings
(priorities run 1-99).

Moves follow the Mega Man X Engine: Ryuenjin plants for 4 frames, rises at
full jump speed for 8 and coasts up under gravity while X is held (releasing
X ends the rise at once); Raikousen is untouchable from its dash to its end.

**Rakuhouha** is the one skill with weapon energy (Chameleon Sting's
energy, refilled like a weapon). KSX Zero keeps his own weapon list, the
buster and Rakuhouha, the way the X2/X3 weapon packs keep theirs: his L and R
step through it and never reach X1's cycle, so another player's weapons are
never touched, and X1's `$0BDB` stays on the buster. With Rakuhouha chosen the
HUD shows its bar (the packs' bar colours) with its own icon, in co-op too,
and R2 performs it. The Giga Attack chooses it, spends 4 of its 28 units (KSX
Zero settings → Rakuhouha energy) and returns to the previous choice when it
ends; L and R do nothing while it runs. Anything that reaches Zero during it
is blocked with X1's deflect sound.

**Rekkyoudan** turns every saber attack Black Zero's purple and gives the
slashes (ground, air, wall, dash), the X3 finisher and wave, Juuhazan,
Youdantotsu and Kuuenbu Boomerang Cutter's contact class, so they cut limbs
(Mammoth's trunk, Octopus's tentacles) and hit Sting Chameleon's weakness. A
skill's own weakness comes first (Juuhazan against Mammoth is Storm Tornado). The saber cuts
physical (destructible) enemy shots and turns energy shots back the way they
came only once Rekkyoudan is learned; before that it does neither.

## Stagger Combos

On by default (KSX Zero settings → Stagger Combos). After a Maverick is hit by
anyone, X and Axl included, it is in a combo for 90 frames from its latest
hit. A KSX Zero move not yet used in that combo breaks through the
invincibility and restaggers it; each hit of one multi-hit swing counts as the
same move and restaggers too. Buster shots count as moves by charge level
(small, full, and the two max shots); a repeated shot does not restagger. A
restagger plays the boss's ordinary stagger, unless the move is that boss's
weakness: then it keeps the weakness reaction (Hyouretsuzan still freezes
Spark Mandrill mid-combo). A
move already used meets the invincibility (no damage) and is left to the
normal hit-priority ladder.

## Settings view

**Values shown** (beside Show hitboxes) switches the damage list between
**Unarmored Zero** and **Black Zero**. Black Zero (all four armor parts) uses
its own damage values in place of the normal ones plus the arm parts' bonus
(+2, +1 against bosses); their defaults are those sums. The launcher reads two
manifest keys for this: `shown_when = "option=value"` and `inline_after`.

## Pause menu

The menu shows Z.BUSTER, the learned skills with their own icons and names in
X1's menu font (in the red of READY), and ESCAPE.U, also in red. It opens on
the chosen weapon, and confirming Rakuhouha or Z.BUSTER chooses it. Only Z.BUSTER, Rakuhouha
and ESCAPE.U keep an energy bar. The right column, with X1's highlight and
sprites, sits 8 px further left so the longest names fit. The hovered item's input command is written in the
middle box, between Zero and the bar, in a narrow form of the title's blue
font.

## Art

`assets/ksx-zero/sprites` comes from the Mega Man X Engine and X8 16-bit (see
`assets/ksx-zero/CREDITS.md`); `tools/saber/build_ksx_zero_sheets.py`
rebuilds the sheets and `tools/saber/ksx_zero_manifest.json` from those
projects; X8 16-bit's Zero reds and golds are set to the X Engine's so
Black Zero recolours every move. `tools/saber/build_ksx_zero_icons.py` builds the pause-menu icons
(`assets/ksx-zero/icons`, `src/saber/mmx_saber_ksx_icons.h`) from a Mega Man
X4 weapon-menu capture and X8 16-bit's pause icons.

Hitboxes cover each move's saber or effect frame by frame (KSX frames are
drawn 7 px above Zero's position) and are placed again after Zero moves each
frame, so they never trail Ryuenjin's rise or Raikousen's dash. All KSX moves share
one record at `$86:FA44` (`$FA80` onward is taken at run time), rewritten
every frame with the running move's box; Rakuhouha's one live object takes
the blast's or a shard's area each frame, whichever reaches an enemy not yet
hit.

## Not done yet

* Turned-back energy shots fly away harmlessly; they do not damage enemies.
* Stagger Combos have not been checked against a boss in play.
* Rakuhouha's block sound has not been heard in play (no enemy reached Zero
  in the tests).
