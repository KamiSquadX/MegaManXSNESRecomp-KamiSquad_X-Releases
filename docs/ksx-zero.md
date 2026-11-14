# KSX Zero

**Zero behavior → KSX Zero**, the default (Add Zero, X / Zero Co-op and X / Zero / Axl
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
menu lists his skills where X's weapons would be. With the packs on, L and R
on his pause menu still reach their pages, but those list only Z.BUSTER and
ESCAPE.U; the packs' weapons are neither shown nor selectable, and leaving
from those pages keeps his own list, so L and R in the stage still bring up
Rakuhouha with its icon. Every skill cancels the
three ground slashes at any point, and some skills cancel others at any point,
however they were reached: Juuhazan into Youdantotsu, Youdantotsu into
Raikousen, Raikousen into Rakuhouha.

| Input | Skill | Learned from | Weak |
| --- | --- | --- | --- |
| Up + X | Ryuenjin: rising fire uppercut, multi-hit; tap for a hop, hold to rise higher; once per jump, whether started on the ground or in the air (again after landing or a wall; landing from Hyouretsuzan does not count, so jumping out of its stab keeps it spent) | Flame Mammoth | Chill Penguin |
| L2 | Rakuhouha: a blast around Zero, then nine shards that each hit on their own; Zero is untouchable for all of it | Sting Chameleon | Storm Eagle |
| Dash + X (or Dash held + X in the air) | Raikousen: lightning dash stab, untouchable while dashing; goes the way Left or Right is held (as Youdantotsu does), also when it cancels another move; once in the air until landing or a wall | Spark Mandrill | Armored Armadillo |
| X in the air, or Up + Y in the air | Kuuenbu: one somersault slash per press | Armored Armadillo | Launch Octopus |
| Down + X in the air (hold) | Hyouretsuzan: ice stab down until landing | Chill Penguin | Spark Mandrill |
| Forward + X | Youdantotsu: lunging stab | Launch Octopus | Boomer Kuwanger |
| X standing still | Juuhazan: overhead swing | Storm Eagle | Flame Mammoth |
| Passive | Rekkyoudan: purple saber | Boomer Kuwanger | Sting Chameleon |

Every hit of a skill counts as the X1 weapon it replaces. It carries that
weapon's shot class (Fire Wave `$0A`, Chameleon Sting `$0B`, Electric Spark
`$0C`, Rolling Shield `$08`, Shotgun Ice `$0E`, Homing Torpedo `$07`, Storm
Tornado `$09`) for both the damage column of the target's X1 row and the
reaction class in `$1F1D`, against every target. Whatever the game keys to
that weapon happens: Ryuenjin breaks the igloos that only Fire Wave breaks,
burns Chill Penguin and does what fire does to anything else; Hyouretsuzan
freezes Spark Mandrill; and so on.

A target is weak to a skill when its damage row favors that weapon above
every other X1 weapon (a Maverick's 3 against 1s, the igloo's Fire Wave,
Storm Tornado on the Sigma stages' bosses), or it is the skill's Maverick.
The hit then deals at least that weapon's X1 damage, on Mavericks, mini-bosses
and enemies alike. The weaknesses are read from the game's table, not listed,
so targets nobody has checked behave as they would against X's weapon.

A target immune to the weapon but not to the saber still takes the skill's
saber damage (the reaction is still the weapon's). An immune target touched by
a swing deflects it once and is left alone for the rest of that swing. X1
tests a shot against one enemy per frame, so without this an immune enemy in
an earlier slot (an igloo under a skill) would keep every enemy after it from
being hit.

In Boss Rush the relocated X2/X3 bosses use their own weakness rules, keyed
by weapon. A skill counts there as its X1 weapon, and a Rekkyoudan move also
counts as Boomerang Cutter.
Each skill has normal damage, boss damage and hit-priority settings
(priorities run 1-99).

Moves follow the Mega Man X Engine: Ryuenjin plants for 4 frames, rises at
full jump speed for 8 and coasts up under gravity while X is held (releasing
X ends the rise at once); Raikousen is untouchable from its dash to its end.
Ryuenjin started on a ladder lets go of it: the rise clears X1's on-ladder
latch (`$0C0C`), as every native ladder exit does, so the fall lands on the
floors below.

**Rakuhouha** is the one skill with weapon energy (Chameleon Sting's
energy, refilled like a weapon). KSX Zero keeps his own weapon list, the
buster and Rakuhouha, the way the X2/X3 weapon packs keep theirs: his L and R
step through it and never reach X1's cycle, so another player's weapons are
never touched, and X1's `$0BDB` stays on the buster. With Rakuhouha chosen the
HUD shows its bar (the packs' bar colours) with its own icon, in co-op too,
and R2 performs it. The Giga Attack chooses it, spends 4 of its 28 units (KSX
Zero settings → Rakuhouha energy) and returns to the previous choice when it
ends; L and R do nothing while it runs. Anything that reaches Zero during it
is blocked with X1's deflect sound. A Giga Attack started from the buster
leaves the charge alone: R2 held keeps charging through it and after it, and
no shot fires until R2 is let go.

A buster charge survives ladders, boss doors, scripted walks and event locks,
as X's does. On a ladder it keeps growing while R2 is held, and letting go
fires from the ladder, as X3's Zero does: X1's ladder actions (`$28`/`$2A`)
run the buster with their own firing pose, so the release is the ground's
(X1's charge state cleared, Zero's shot handed over as a fire command). The
whole burst works there: the first shot on release, the second on the next
press, then the X3 finisher and its wave from the saber button in the
window. X1 refuses a charged command while a charged shot lives (`$81:94AF`
checks `$0C25`), so the second shot is Zero's own object, as the ground
burst's is, shown in X1's ladder firing pose (sequence `$4A`, shooting set
`$0C17` = 0, pose timer `$0BF8`). The finisher is the airborne swing, as in
a jump. A burst's follow-ups survive grabbing or leaving the ladder. There,
and everywhere else, the charge holds its level and a release waits, as it
does during a hit, for the first frame Zero can fire again (the door
finishing).

**Rekkyoudan** turns every saber attack Black Zero's purple and gives the
slashes (ground, air, wall, dash), the X3 finisher and wave, Juuhazan,
Youdantotsu and Kuuenbu Boomerang Cutter's contact class. Every target weak to
Boomerang Cutter (Sting Chameleon, and any whose row favors it) is weak to
them, and against bosses they cut limbs (Mammoth's trunk, Octopus's
tentacles). On those three skills the skill's own weapon wins where it is the
target's weakness (Juuhazan against Mammoth is Storm Tornado). Otherwise
Boomerang Cutter wins where it is the weakness, against bosses, and where the
target is immune to the skill's weapon. The skill's weapon is used everywhere
else. The saber cuts
physical (destructible) enemy shots and turns energy shots back the way they
came only once Rekkyoudan is learned; before that it does neither.

Every saber attack breaks Sting Chameleon's fallen rocks once they land, as
X's weapons do. The rocks are terrain tiles that X1's rock object
(`$83:BD6A`) breaks around each player shot; the saber takes one of its turns
per tile its hit area covers (`tools/apply_saber_rock_hooks.py`). Each turn
costs about 28,000 of the SNES CPU's master cycles out of the 357,000 in a
frame, so every turn but the last gives its time back: a slash costs the game
what one shot does and never drops a frame.

## Stagger Combos

Off by default (KSX Zero settings → Stagger Combos). After a Maverick is hit by
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
hit. Co-op puts the records back each time it switches seats; the KSX record
comes back with the box last written to it, not empty, because enemies are
hit-tested after the other seat's turn (an empty record never overlaps an
enemy).

## Not done yet

* Turned-back energy shots fly away harmlessly; they do not damage enemies.
* Stagger Combos have not been checked against a boss in play.
* Rakuhouha's block sound has not been heard in play (no enemy reached Zero
  in the tests).
