# Zero on the X2 page: port plan and findings

Status (xx0.3.1alpha, released): **the dev builds xx0.1.10alpha to
xx0.1.21alpha, released for Windows, Android and Switch.** Android: the
On-screen Gamepad is an input source for player 2 too (one player at a
time; picking it for one player sets the other to None), so player 1 can
use a real pad while player 2 plays on the touch screen. The touch pad
drives the seat whose source is 3 (touch_gamepad.inc TouchPadSeat), through
that player's gamepad bindings.

Status (xx0.1.21alpha): **Widescreen on the X2 page; screenshots of the
whole frame; Tier 2 diagnostics on both programs.**

* Mega Man X Widescreen (xx1.0.0, "mstan; X2 support by KamiSquad_X"): X2's
  host is native widescreen. The view is X1's (MmxRendererViewport, the
  mod's aspect option; Side by side: each pane as X1's), 256 in X2's
  native-frame room ($1FAD 12), recorded and replayed like X1's width. X2's
  own widescreen passes from MegaManX2Recomp are back in x2_runtime.c: HUD
  anchoring (X2ConfigureWsHud), margin backgrounds (X2ConfigureWsBgMargins
  with snesrecomp's mmx23_background, cherry-picked from the X2 project's
  pinned framework: 9473530b 6f83dc1f 0c86f5f2 0f68883a a77c1f72 d980d852
  dec36f37), Wire Sponge's BG3 weather repeat, and the object windows (Side by
  side: the larger of the co-op leash and the widescreen margin).
* The host takes a field width per game (SnesDesktopHostGame.field_width):
  Side by side, X2's field is one pane of the two-pane frame.
* Overlays draw in screen coordinates: screen_row is renderBuffer +
  extraLeftRight (it was the leftmost drawn column, the same only at 4:3).
  Zero's canvas spans the margins; Zero's badge, player 2's meter and blue
  health move with the anchored HUD (X2WsHudLeftShift).
* Script `dump` also writes <tag>.screen.bmp: the frame as shown (both panes,
  margins). <tag>.fb.* stay the native 256 columns (snesref comparisons).
* Tier 2 diagnostics: X1's coverage capture never started (its flag was read
  in main before mods activate); the mod's switch now sets both programs'
  flag and the running capture. Replays record X2 since xx0.0.17alpha.
* Escape Bugs: xx1.0.0.

Status (xx0.1.20alpha): **frame captures on the X2 page.**

* `SNESRECOMP_FRAME_BMP` / `_DIR` captures stopped at X2's stage select: X2
  counts its own frames (the number drops from X1's to X2's), and the
  capture waited for the old number. It now follows the number after a jump
  back or a long jump ahead (`snesrecomp/runner/src/widescreen.c`).
* X2's frame (side by side included) is presented through
  `RtlWidescreenPresent`, so captures see both panes.
* Checked on the Bubble Crab fight of a co-op session with no X1 progress:
  KSX Zero's saber is green. (Save slot 1 of the test folder has X1 progress
  with Rekkyoudan learned, where purple is correct.)

Status (xx0.1.19alpha): **a pass across a lag frame; Zero's upgrades from X1
only; no X2 weapons for Zero.**

* A lag frame (Bubble Crab's mini boss arriving) can end inside a pass: X2's
  routine yields at the frame's end and goes on in the next. The frame start
  cleared the terrain pass's flag while player 2's body was in $09D8, so the
  pass's end ($08:C325) never put player 1 back: X vanished and port 1 drove
  Zero. The frame start now leaves a pass with the other seat live alone.
* KSX Zero on the X2 page: skills from X1's record only (no record: none),
  and his armor parts ($1F99 bits 0-3 in his view: the Saber's purple arms)
  from X1's $1F99 as last recorded, not X2's capsules ($1FD0 bits 0-3, which
  the write-back now keeps as X2 had them). X1's armor rides in the bridge
  chunk's head (byte 10; older snapshots: none).
* No X2 weapons for Zero (his X2 page skills come later): X2's weapon switch
  ($08:BED3, L / R, both, Up + Down) is skipped for his body (to its RTS
  $08:BECB; +$33 kept 0), and while X2's menu is up for him its weapons'
  owned bits ($1FB9 + 2n bit 6, n 1..10) are taken away and put back as it
  closes (bridge head bytes 11-12).
* Player 2's pause in co-op: the close's fade ends in $00:CB7E, equipping
  the menu's choice ($1EE2) on $09D8, player 1 by then; $1EE2 is set to
  player 1's own weapon as the main role goes back.

Status (xx0.1.18alpha): **Wheel Gator's oil holds player 2.**

* The oil in Wheel Gator's room is no terrain or solid object: its object
  ($05:8639, through $00:FDB3) ends each frame in $05:87AF, which puts player 1
  ($09DD/$09E0) on the surface from a wave table ($6B00 + x/2 + $1F57,
  low bytes only) and sets $0A04 bit 2. Player 2 sank through it (it only
  stood on the walls), and the Gator's grab of player 1 ($1F54 set, $0A3C
  in the held body) skips that call for everyone. At the routine's RTL
  ($05:8654: generated-code patch and interpreter twin) the same check runs
  for the other seat's body, unless the Gator holds it (+$64).

Status (xx0.1.17alpha): **revives near a boss door; player 2's Start as
the main seat.**

* The beam-in's end ($08:A624: JSL $00:E67B) sets the camera's top limit
  ($1E6A/$1E78) from the checkpoint's table, right at a stage's start.
  For a co-op arrival (a revive, a door's far side) past a boss door it
  left the camera between two rooms (Flame Stag's: Y 1024 against 544 every
  other frame); the players were off screen and the one placed again fell
  as in a pit. With the other seat on the stage the call is skipped
  (generated-code patch and interpreter twin).
* A beam whose column has terrain over the partner's level (the low
  corridor past a boss door) starts at the partner's height, not on top.
* X2's pause ($00:9F3E) wants Start in the main seat's new buttons ($0A13
  bit 4). Player 2's input as the main seat (player 1 down) left Start out,
  and the main pass's set-up runs twice a frame, so the second call saw any
  new button as held: Start's edge comes from the frame's own press.
* Checked with the user's replay (segments 28 and 31: one camera height,
  nobody falls) and a script (player 1 down, player 2 opens Zero's menu).

Status (xx0.1.16alpha): **Flame Stag's rising lava burns player 2.**

* The lava (object id $2E) tests the player in its own routine, $07:E22A
  (JSR from $07:E15F, RTS at $E264): a player below the surface ($09E0 >=
  its y) is pushed up a pixel and takes 8 (or $7F 256 px under) through
  $08:DBE4, unless invincible ($0A62). It never called the touch test, so
  only the main seat was ever burned; player 2 stood in it unharmed and died
  at once on becoming the main seat. It now runs once per seat like
  $08:CC7F (the solid-object pass). Checked with the user's replay.

Status (xx0.1.15alpha): **player 2's health pickups and pause menu.**

* Health ($01:D58D states D59A/D62A/D65F) and Heart Tank ($01:E488 states
  E4BD/E4CA/E502) pickups run over several frames: the touch freezes the
  world and holds the collector ($08:DC64: $0A46 set, action $18 via
  $0A7B), later states refill $09FF a unit at a time and release it
  ($08:DC9C). Only the touch tests the player, so the later states acted on
  player 1: player 2 kept its health and stayed held for good. At those
  states' entries (generated-code block hooks) the held seat is put in for
  the rest of the item when the other is not held.
* Pause: X2's menu shows the main seat. Player 2's Start is port 1's
  Start; once the pause begins ($1F21 set) player 2 is the main seat, until
  $1F21 is back to 0 (or 30 frames without a pause). Handing the main seat
  over on the press itself lost the press.
* Checked with the user's replay (health refill 6 to 14 for player 2, then
  moving) and the P2 Start script (Zero's menu; X's for player 1).

Status (xx0.1.14alpha): **player 2 takes Sub Tanks without leaving.**

* X2's player action $18 is its frozen pose for any world freeze (Sub Tank
  and Heart Tank pickups, boss intros, a door's start); a boss door's walk
  is $18, $1C (through the door), $1E. Only $1C sends the other seat out to
  beam in beyond the door; $18-$1E still hold it in place. Before, a Sub
  Tank player 2 picked up froze the main seat in $18, which took player 2
  off the stage until it beamed back in. Checked with the user's replay.

Status (xx0.1.13alpha): **both players on one pillar; a partner's death no
longer freezes the stage; the buster after a restart.**

* **Solid objects, per seat:** $08:CC7F (the solid-object routine: touch
  test, push out, landing on the top with the object's .2C bit 0 and the
  player's $0A04, crushing) and $08:D651 (touching: $0A04 bits 7/6) run once
  per seat from the same entry registers and .2C; the .2C answers are OR-ed
  and the caller goes on with player 1's scratch ($0000..$001F). Both run
  interpreted while a second seat could answer (generated-code dispatch in
  tools/apply_xtreme_tank_hooks.py). $08:CD2E (the rider carry: the object's
  movement added to the player, called while .2C bit 0 is set) moves exactly
  the seats that landed on that object this frame. Wire Sponge's pillars
  (object id $2C, $07:DCAE/DCB2/DDF7) and the id $20 blocks ($04:9920/9924)
  use them. Before this, the touch test's fallback gave player 2 a turn only
  when player 1 missed, so player 2 fell through a pillar player 1 landed on.
* **The world's freeze flags ($1F25..$1F2B):** a player's death sets them
  (objects stop, a pillar with them). Put back as the pass found them after
  a death that leaves the other playing: player 2's pass and terrain run,
  and the main seat's death handover. Only the last player's death freezes.
* **Shot count:** settle_shots compares $0A0D as a signed byte. The other
  seat's shot ending while the main seat had none out left $FF (255 shots
  out, no buster until the stage ended); now it is given back the next
  frame, and a count below zero is never kept.
* **Revive:** Select while fallen stays pending until the other seat can
  receive it (not hurt, not in a door or an event).
* Checked with the user's Wire Sponge replay (segments 63-64): both ride the
  pillar down and back up with no deaths; player 1's count recovers from
  $FF in one frame and the buster fires.

Status (xx0.1.12alpha): **X2 co-op objects, doors, pits; per-player blue health.**

* **Objects testing the player ($08:D865, X1's $84:9C0E, byte for byte):**
  51 callers pass X = $09D8 (item drops, solid blocks: $08:D654 / $03:A836
  push and carry through $0A04, spotlights, event boxes, boss doors). While
  player 2 plays, the generated code runs it interpreted; on a miss for
  player 1 it is run again with player 2's body, and a hit keeps player 2 in
  the player struct until that object's update ends (the interpreted object
  loops' returns: items $00:CFB6/CFE4 at $1518 step $30, enemy shots
  D16F/D19D, enemies D1D6/D204 at $0D18 step $40). Checked: an object moved
  onto player 2 (player 1 away) reports the touch. Limit: when both touch,
  player 1 is the one (a block pushes player 1 only).
* **Boss doors ($01:DCD1 / $01:DFD6, from $01:DCE0 / DFE5):** player 2
  touching one becomes the main seat (the door walk, scroll and boss are
  its); the main seat's door walk (action $18-$1E) sends the other out, to
  beam in beside it once the door and its event locks ($1F1D / $1F36) are
  over. Not tested on a real door.
* **Vertical, as X1 ($00:E12D):** at $00:E0EA (main seat below the camera's
  bottom: death) the shared camera above is not a pit while the main seat is
  above the room's lowest camera; the other seat below that ($1E6C + 224)
  dies as in a pit.
* **Hitbox overlay:** both players' boxes (the X1-layout view holds one).
* **Blue health per player:** each seat's own pool (X1 co-op and X2 co-op
  bodies, live or stored); drawn over each meter (X2: the main bar and
  player 2's meter, x+3, 6 above its badge; X1 co-op: x+5, 2 above). A new
  heart past eight fills a unit per tick (X1 4 frames, X2 2) with the tank
  refill tick's sound. Health a pickup gives a full bar (the Sub Tanks'
  rise) goes to the player missing the most blue first. Heart pickups past
  eight also hook the interpreted path ($81:EAEA / $01:E477 twins).
* **Escape Bugs teleport:** the player who pressed Select moves (a menu
  player 2 opened gets its buttons in player 1's place, so player 2 is
  checked first; Start closes from player 1's place too). X and Select act
  once the menu has closed (the menu keeps the live camera aside while it is
  up). Landings: left, right, middle, back on the path (where that player
  last stood on ground in this stage), the other player.
* **KSX Giga on X2:** player 2's Giga press now reaches KSX (learned,
  energy, grounded all pass); no freeze reproduced; Rakuhouha does not show
  on the X2 page yet.

Status (xx0.1.11alpha): **Escape Bugs rework, X2 co-op input, blue health.**

* **Escape Bugs restart:** no death any more. X1's death state ($D3 = 6,
  $00:9B23) and X2's ($D2 = 6, $00:99AF) wait for "death done" ($1F0B /
  $1F1C), then fade, take a life and reload at the checkpoint (the stage's
  start when none). The restart sets that state and flag with a life added:
  an immediate fade, nobody's health touched (the X1 co-op soft lock came
  from zeroing both bodies). Every command closes the menu first (Start,
  the cursor back on the weapon it opened on; X2's column sub-state $1EDA
  follows the cursor). X2's EXIT is cursor $1E with $1EDA = 4 ($1A was the
  third Sub Tank). New: Start + Select 5 s anywhere in a stage sets the exit
  state (X1 $D3 = $0A, X2 $D2 = $0A), with a progress bar; Start and Select
  stay out of the game meanwhile (no pause, no CHANGE PLAYER).
* **X2 co-op input:** player 2's Start pauses and drives the menu it opened
  (MmxX2CoopMenuInputs). KSX's extended buttons (Giga Attack, R2) were read
  from X1 co-op's seat on the X2 page, so player 2's Zero read player 1's;
  now X2 co-op's Zero seat. The pause page (X2 / X1) is remembered; in play
  L / R work on it only (X keeps the buster on the X1 page; Zero's
  Rakuhouha only there); Start on a skill row chooses it from the buster's
  place. The Giga freeze itself was not reproduced (the test setup has no
  KSX).
* **X2 co-op revive:** beams in (X2's action $22) from the screen's top onto
  ground beside the other player at its level (32-96 px, nearer side first);
  the life is taken only when the revive happens. Zero's canvas sorts after
  X's last OAM piece, so X is always in front.
* **Netplay:** X2's background boot runs online too (deterministic per
  peer), so the first page turn needs no boot inside the held switch; the
  host no longer shows "Changing games". Not tested between two peers.
* **Heart Tanks past eight (up to 24):** the pickup (X1 $81:EAEA, X2
  $01:E477) skipped the chime and health-up at 32 max HP; with room it now
  goes the full way. Past eight, each heart is 2 HP of blue health over the
  bar from its bottom (x 13-18, 2 px per HP from y 78), taken first by hits
  (the yellow bar is topped up from it), full again at each stage start or
  restart. The main player only in co-op. A real pickup past eight was not
  seen in a test (hook in place; the blue health was).

Status (xx0.1.10alpha): **X2 co-op fixes, Escape Bugs, test watchdog.**

* **X2 co-op shot count:** X2's shot routines take a finished shot off the
  main seat's count ($0A0D) whoever fired it, and only the routines that
  count (Zero's saber objects never do). The old fix gave X a shot back for
  every one of the other seat's objects that ended, so X's count climbed
  past 3 and X stopped firing. Now the drop is measured: the main seat's
  count and live slots as its pass ends, against the next main pass; what
  X's own ended shots don't explain goes back to X (and off the other seat).
  Checked: X and Zero firing together for 6 rounds, X's count back to 0
  after each shot; P2's L/R leaves X's weapon ($0A0B) alone.
* **X2 co-op deaths:** the main seat falling while the other is alive hands
  the main role over at $00:990E (before X2 sets death mode $D2 = 6), so the
  stage goes on. The fallen player comes back with Select once its death has
  played out (180 frames), for an extra life ($1FB3), beside the other
  player. Checked: P1 falls, Zero plays on, Select brings X back, lives 2 to 1.
  P2's meter is the non-main seat's (it showed the main seat's after a
  handover).
* **X2 co-op sprites:** X's pose tiles are never replaced by Zero's pass
  (checked over Zero walking, jumping, slashing and passing through X).
* **Escape Bugs** (Accessibility, on by default, src/mmx_escape.c): with
  ESCAPE.U (X1 item 9) or EXIT (X2, cursor $1A) highlighted, X resets the
  camera (a second X opens the room a screen wider each way), Select moves
  to standing ground on screen (left, right, middle in turn), holding B 3 s
  restarts at the checkpoint without losing a life, holding Start 3 s goes to
  stage select from any stage. A short B or Start is the game's own. The
  list shows over the pause screen. Escape check hooks: X1 $00:C93C (answer
  from $C94E), X2 $00:C622 (answer from $C641; X2 calls it from EXIT's
  sub-state only, $1EDA = 4 at D = $1ED8, so a held Start sets it). Its
  bytes ride in OPTION MODE's state (both games' snapshots). Checked in X1
  (solo Zero, Spark Mandrill) and X2 co-op (X + Zero); X1 co-op's restart
  (both bodies to the end of their death, as Boss Rush's escape does) is
  not checked yet.
* **Test watchdog:** a `--script`, `--replay-diagnostic` or `--benchmark`
  run ends itself with exit code 124 and a `[mmx-test] HANG` line when no
  frame advances for MMX_TEST_STALL_SECONDS (60) or it outlives
  MMX_TEST_TIMEOUT_SECONDS (1800). tools/replay_diagnostic.py stops a run
  after 2400 s and reports the hang.

Status (xx0.0.19alpha): **Flame Stag soft lock, final Xtreme tank.**

* **Flame Stag soft lock (X2's own):** climbing the ladder up into the
  X-Hunter room (around x $0938, y $04AF) starts its boss intro: action $1E
  ($08:B507, from $0A7D bit 5), whose substate 4 waits for the intro's
  script, which never comes from that side. When $1E follows the ladder
  ($26/$2A) at that spot in stage 4 and nothing lets go for a second, the
  player is set standing ($00). Verified on the user's replay (it was
  recorded on the X2 page and plays back matched).
* **Xtreme tank final art** (docs/screenshots/xtreme-sub-tank-draft7-final.png):
  nine glass rows, each its own colour. A full native tank becomes a full
  Xtreme tank (units x 32 / 14).
* **Drain:** the proxy nibble now holds the units that heal what is missing
  (up to the energy), so the native use refills it with its own pace and
  tick sound (X2: 2 HP a unit, a tick every 2 frames, sound $15; X1: 1 HP a
  unit, every 4 frames, sound $0C). Past 15 units the rest follows at the
  same pace and sound. The tank shows energy draining from the top.

Status (xx0.0.17alpha): **replays on X2, Xtreme Sub Tank art.**

* **Replays record X2:** X2's frame_begin / frame_end call the recorder; each
  checkpoint has a `P <program>` line before its `S` line (0 X1, 1 X2, 2 X2's
  password page). Playback turns to that program first (one unrecorded,
  buttonless frame while the page turns), then loads it. X2's snapshots now
  also carry the shared-items tracking and OPTION MODE's tap state (bridge
  chunk), which replays need to match. Verified: segments in X1, across the
  X1 to X2 turn and in X2 all match.
* **Xtreme Sub Tank art:** traced 26x23 from the reference; one black window
  per tank (rows 6-11 and the strip at row 16), filled with the native tank's
  seven colours (read from X2's full tank). Lower tanks are cut at the native
  tank's bottom (above SUB.T); X1's columns move 1 px apart.
* X2 cursor sound `$1C` on the X1 page's skill rows.
* Xtreme tanks follow each menu's cursor (X1 `$1ED2` items 10-13, X2 `$1EE2`
  $16/$18/$1A/$1C): unselected ones lose 8 (X1) / 12 (X2) per 5-bit channel,
  as the native tanks do. No trimming; with tanks 3-4 Xtreme, X2's SUB.T and
  EXIT move down 4 px (rows 125-131) and the lower tanks sit 1 px higher.

Status (xx0.0.15alpha): **passwords for both games, tanks 5-8, X1 page cursor.**

* **X2 Password Saves:** X2 shows a password only after a game over, so its
  progress is saved from the stage select: half a second after coming back
  from a stage, X2's generator (`$00:F013`, D `$1E58`, DB 6) runs on a copy of
  the CPU and WRAM is put back. Start on X1's password page now checks X2's
  saved password too (not only a visited X2 page), so one Start loads both.
* **Sub Tanks 5-8:** the pickup ($01:DBF6 X2, $81:E643 X1) finds no free slot
  once four are owned and falls through ($DC15 / $81:E662) without the chime.
  `tools/apply_xtreme_tank_hooks.py` hooks that block (generated code, plus
  interpreter twins): the pickup flag is set and the success path ($DC1A /
  $81:E667) runs. `xtreme_frame` then upgrades the next slot.
* **DOUBLE TAP DASH on X2:** X2 keeps X1's own second-tap dash ($08:BB06),
  which ignored the option; the same tool disarms it (D+$53 cleared), leaving
  OPTION MODE's.
* **X1 page cursor:** X2's pause cursor is `$1EE2` (item x2; $16 on = tanks
  and EXIT). On the X1 page the left column (X.BUSTER, learned skills) is
  driven here; the right side stays X2's. Skills sit on X2's 16 px rows
  (y 17 + 16n), unselected ones dimmed; rows 9-10 stay free.
* **Saber:** Kuuenbu re-hits every frame. A deflect (a met under its hat) no
  longer ends an attack's tries on that enemy: it is skipped for four ticks
  (`immune_slots`, in the state's padding: snapshot layout unchanged).
* Not verified in play: a 5th tank pickup, mets in Flame Mammoth's stage.

Status (xx0.0.14alpha): **launcher, options, Xtreme Sub Tanks, KSX fixes.**

* **In-game launcher on the X2 page:** X2's host descriptor has
  `in_game_launcher`; the host seeds and commits the launcher from the
  session's first descriptor (`g_root_game`), so it is always X1's and its
  settings apply to both pages. A restart (mods, renderer, audio rate) goes
  through `save_resume_state` / `on_resume_state` (new descriptor fields):
  `resume.sav` is X1's moment (its parked state when X2 runs) and
  `resume.sav.x2` holds X2's page, title, password page and the shared
  progress; X2 is turned back to after the restart if it was running.
* **OPTION MODE / KEY CONFIG on X2:** KEY CONFIG was already the shared
  `map_seat_input`. DOUBLE TAP DASH and HOLD DASH run on X2's frames
  (`MmxOptionsX2FrameBegin`; HOLD DASH off masks held dash on a wall, action
  `$12`, unless pressed in the last ten frames). SOUND MODE is `$7E:FFCA`
  (`$F7`/`$F8`) in both games; X1's is copied at each turn to X2 and queued
  in X2's sound ring (`$09A2`, index `$09D3`).
* **KSX on X2:** X2's box test reloads the shot's `+$20` at `$88:D89B`, so the
  KSX record (X1 `$86:FA44`, X2's own data there) was read as a huge box. The
  hitbox hook now writes the published pointer (`$86:FE00`) into the shot.
  Zero's terrain queries read X2's map (`MmxWeaponsSetTerrainSource`: screens
  `$E800`, blocks `$2800`, class table pointer `$09C2`), which fixes Ryuenjin
  stopping under ceilings that were X1's. Ryuenjin re-hits every frame
  (Fire Wave's rate) on both pages.
* **Zero's X1 page in X2's pause:** his KSX skills in X1's order, one column,
  X2's own font (BG1 tiles A-N `$72-$7F`, O P `$82 $83`, R-Y `$84-$8B`, `.`
  `$8C`, palette 2; no Q or Z, Z drawn in its shading).
* **X1's X2 weapons page:** unearned weapons are not drawn (icon, bar, name)
  and L / R in a stage skip them (`MmxWeaponsPageWeaponOwned`). Package
  `megaman-x.xtension.x1-x2-weapons` is xx0.0.2.
* **Xtreme Sub Tanks** (`mmx_x2share.c`): tanks five to eight upgrade slots
  1-4. Energy (0-32 HP) is kept outside WRAM; the slot's nibble is a proxy
  (0 empty, 1-13, 14 full). Native fill adds units (2 HP). Native use heals
  from its own copy of the nibble and then re-sorts the slots, so after it
  settles the HP is corrected to what was missing (up to the energy), the
  rest kept, and the slots restored. Drawn over both pause screens (X1 tanks
  at 32/56 x 142/166, X2 at 136/168 x 81/105, 16x16 BG cells) as a 24x20
  double tank. Energy goes with X2 snapshots (bridge head bytes 6-9).
* Not verified: a tank use on X1's page (the script could not move X1's
  pause cursor), the launcher restart path from X2, HOLD DASH off on an X2
  wall.

Status (xx0.0.13alpha): **shared progress, passwords, pause pages.**

* **No first-turn stall:** X2 is booted to its stage select in the background
  while X1 runs (about 3 ms after each X1 frame). A slice parks X1's WRAM and
  host residue (`RtlHostResidueSave/Load`, new in snesrecomp) and runs X2 on
  its own machine with its DSP muted. Jobs: boot, X2 password page (from the
  title the frame before GAME START, cursor `$3C` = 1), password check,
  password finish. Netplay still boots inside the synchronized switch.
* **Select exchange in X2** no longer skips whole frames: X2's NMI runs with
  `$09CE/$09D0` cleared (X1's `$0B9D/$0BA0`) and only the tasks wait, so
  music and screen keep going. Tier 2 diagnostics still costs ~0.3 s on the
  first X2 frame (its coverage files).
* **Shared items** (`mmx_x2share.c`): X1 lives `$1F80`, tanks `$1F83..86`,
  tank flags `$1F99` hi, hearts `$1F9C`, max HP `$1F9A`; X2 `$1FB3`,
  `$1FB6..B9`, `$1FD0` hi, `$1FD3`, `$1FD1`. Max HP = 16 + 2 x hearts of both
  games (cap 32), tank slots = tanks of both (cap 4), lives and tank energy
  carried at each stage-select page turn.
* **X2 Password Saves (SRAM)** (`mmx_x2pass.c`, page turn in `mmx_x2ext.c`):
  X2 entry screen D = `$1E58`, digits `$1E74..83` (16), state `$1E59` (4 =
  input), prefill hook `$00:EE6F`, capture `$00:EFE1` (`$7E:FFCB..DA`),
  dispatcher `$00:EDD3`, Start check `$00:EEE5` (Z = good). X1: D = `$1E48`,
  `$00:EE8D`, `$00:EF7B` (A = 0 good). X2's generator is `$00:F013` (DB 6).
  Offline only.
* **Zero's X2 pause:** X's figure (OBJ `$1E0..$1F7`, attr `$33`, top right)
  is replaced by Zero's menu pose. X2 pause = `$1F21` 6/8 with `$C4` bit 7.
* **X1 to X2 boss weapons** (`mmx_x2pause.c`): L/R pages in X2's pause (X1
  page blank for now, cursor buttons held); X1's X2 page offers a weapon once
  its X2 boss (`$1FBB + 2n` bit 6) is beaten. Zero's view on the X2 page now
  carries X1's weapon bytes `$1F87..98`, so KSX Special Moves follow X1's
  beaten Mavericks.
* **Co-op physics diagnostics** writes `logs/coop-x2physics-*` (and
  `coop-x2netplay-*`) on the X2 page; label xx1.0.1.

Status (xx0.0.11alpha): **netplay co-op on the X2 page** (Unified and
Independent cameras).

* **The page turn is synchronized.** In netplay only the host (player 1)
  turns the page (R / L on the stage selects; the host's own buttons are
  never a prediction). snesrecomp's netplay gained a program-switch command
  on top of its synchronized savestate hold (`snes_netplay_program_switch`,
  `NP_CMD_SWITCH`, the hold's tick flagged with `NP_HOLD_SWITCH` so no menu
  is shown): every peer holds on one agreed tick and its state is checked
  against the host's; the host switches, then every guest (the switch runs
  from each peer's host loop, `RunProgramSwitch`, and the peer reports
  `snes_netplay_program_switch_done`); the new program's state is checked
  again and the host's is sent to a peer that differs (an X2 visited before
  the match, for example); play resumes in a new input/rollback epoch.
  "Changing games" is shown meanwhile.
* **X2's first visit** cold-boots X2 for about 840 frames inside the switch;
  it calls `snes_netplay_keepalive` as it goes, or the peers time each other
  out (1.5 s of silence).
* **X2 snapshots carry Zero, KSX and co-op** (`X2StateSaveExtra`, chunk
  `x2BZ`: Zero's live state, the KSX extension's 2048 bytes, the exchange's
  edge and pad, co-op's seats and pass state, `mmx_x2zero.c`
  `MmxX2BridgeSaveState`). Rollback restores them with the machine; so do
  rewind and save states on the X2 page. Player 2's camera is presentation
  and stays out. A program swap restores X2's machine before
  `MmxX2ZeroEnter`, which keeps its own copy, so the chunk is not applied
  then.
* **Netplay cameras: Independent** runs the Side by side rules (leash,
  widened object window) on every peer, and the guest shows player 2's
  camera (the pane, full size, with player 2's meter); the host shows player
  1's. Unified is one shared screen, as locally.
* Checked with two local peers (scripted pads, per-tick hash of WRAM and the
  seats on both): synchronized page turn, the X2 state matching after it, and
  identical state for 3,000+ ticks with player 2 moving, slashing and hit,
  including 64 forced rollbacks; Independent the same, the guest's view
  following player 2.
* Known: with the link simulator's jitter (packets reordered), the new epoch
  after a synchronized hold can stall both peers; X1's own synchronized
  savestate load stalls the same way under that setting. Latency alone and
  plain links are fine.

Status (xx0.0.10alpha): **local co-op, Side by side** (`src/x2ext/mmx_x2split.c`).

With **Local co-op display: Side by side**, the X2 page presents two 256x224
panes (5:4 each, 5:2 together, as in X1). X2 still runs one camera, which
follows player 1: its PPU field is player 1's pane. Player 2's pane is a
second raster of the same frame:

* **Start state:** `X2DrawPpuFrame` copies the PPU before the field's first
  line; after the field, the pane is drawn from that copy into its own
  buffer (host surfaces cleared), with `g_ppu` pointed at it and the DMA unit
  saved and restored. HDMA runs again for the pane; a scroll value HDMA
  writes gets the pane's offset.
* **Backgrounds from the layout:** VRAM holds only the tilemap around player
  1's camera, so BG1 and BG2 are rebuilt for the pane's region from the
  stage layout, X1's renderer formula at X2's addresses: screens
  `$7E:E800` / `$7E:EC00`, 16x16 blocks at `$7E:2800` (BG1, X1 `$2000`) /
  `$7E:A600` (BG2), 8x8 tile table at the long pointers `$09C5` / `$09C8`
  (X1 `$0B95`). Checked against VRAM in X2 dumps: 896/896 tiles. BG1's plane
  is the camera (`$1E5D` x, `$1E60` y); BG2's plane is its own scroll
  (unwrapped), moving at a rate measured over 32 pixels of camera movement
  (half in the jungle opening). A layer whose tilemap does not match the
  layout at player 1's camera is only scrolled.
* **Camera 2** follows player 2 with a dead zone (`$70..$90` x, `$60..$A0`
  y), clamped to the room's current camera limits (`$1E66` left, `$1E68`
  right, `$1E6A` top, `$1E6C` bottom; targets at `$1E6E..$1E7E`) and to a
  leash of 128 px across / 64 px up and down from player 1's camera. It
  eases back to player 1's camera while player 2 is fallen or away.
* **Objects stay alive in player 2's view:** player 2 is held within the
  leash (and the room), and X2's widescreen object window
  (`x2_ws_spawn_margin`, the `X2WsObjWin*`/spawn-stream immediates) is widened
  by 160 px around player 1's camera. Enemies there spawn, act and are in
  OAM (off player 1's 256 columns, so not drawn in player 1's pane).
* **Sprites:** X2's OAM moved to camera 2. An entry's 9-bit X is read on the
  side the pane looks toward. Player 1's meters (tiles `$80..$8F`, priority 3,
  x < 48) are left out; other HUD (boss meters) stays put. Zero's seat is
  drawn for camera 2 (`MmxX2ZeroPaneObjects`).
* **HUD:** each pane has its own player's meter where player 1's is (player
  2's from X2's HUD tiles, Z badge for Zero); the Horizontal/Compact choice
  applies to Unified.
* Outside a stage (stage select, menus, scenes) both panes show the shared
  frame.

Not done for Side by side: vertical object windows are X2's own (objects far
above or below player 1's screen are not active), BG3 is not moved, and
tiles a stage writes into VRAM by itself (not from the layout) show as player
1's in player 2's pane.

Status (xx0.0.9alpha): **local co-op, Unified screen** (`src/x2ext/mmx_x2coop.c`).

X1's co-op runs the player routine once per seat with that seat's body
projected into the player struct; X2's is done the same way in X2's terms,
without porting `mmx_coop.c`:

* **The whole player struct is $110 bytes** (`$09D8..$0AE7`): X1's `$90`
  plus X2's own `$80`, which holds the hurt/death request (+`$A3`, `$0A7B`,
  `$8E` hurt / `$8C` death, set by `$08:D963`) and invulnerability (+`$88`,
  +`$8A`). Swapping only `$90` made player 2's fatal hit kill player 1.
* **Player 2's pass:** generated hook before the player routine's PLD at
  `$08:94C3` (block `$08:94AD`, after `STA $11`) and an interpreter twin:
  undo the prologue's PHD/PHP, swap in player 2's body, helpers (`$0AE8`,
  `$60` bytes) and Zero state, map player 2's pad, restart at `$08:9428`.
  Before it: X2's `$00:CE7E` work for that body (+`$22`/+`$24` previous
  position, `$1F1E` = 0); after it, +`$2C` = 0 (`$00:CEA8`).
* **Input:** X2's mapper `$00:E52E` is X1's `$00:E543` byte for byte (masks
  `$7E:FFC0..C5`, joypad `$A8/$A9`), so X1 co-op's formula fills +`$36/38/3A`.
  KSX Zero reads buttons through `MmxCoopSeatButtons/SeatPad/CurrentSeat`,
  which now take an override (`MmxCoopSetSeatOverride`) from the X2 seats; the
  NMI joypad copies `$A8..$AD` hold player 2's pad during player 2's pass.
* **Terrain contact is outside the routine:** `$08:C314` (D = `$09D8`, after
  the object loops) runs `$08:CD5E`, which sets +`$2B` (ground). Without a
  second run player 2 "landed" at the top of every jump. Generated hook at
  its PLD (`$08:C325`) undoes the PHD and reruns it for player 2.
* **Enemy contact** `$08:D752` (X1 `$84:9B03`) is **generated** (variants
  M1X0/M1X1, `bank08_part0a`): hooks at its entry and before each of its 8
  RTLs per variant restart it for player 2 after player 1's pass (entry P
  restored), keeping the first contact found (X1's contact passes). M0
  entries run interpreted (pre-opcode twins).
* **Shots are shared** (not projected): both players' shots are live objects,
  so either player's shots hit enemies natively.
* **Zero state per pass:** `$08:9479`/`$08:947E` select the live seat's Zero
  state (`MmxX2CoopPassBegin`); outside the passes Zero's seat owns it and
  the bridge's view shows Zero's stored body (`MmxX2ZeroSetPlayerSource`), so
  shot ticks, hit hooks and drawing see Zero.
* **X's tiles:** player 2 = Zero skips `$08:C9B8` (pose CHR upload, redirect
  to the RTL at `$08:C9B7`) so X's VRAM tiles stay player 1's.
* **Death:** player 2's death plays out (orbs) but `$08:A71C`'s `INC $1F1C`
  (stage restart) and the fade to white (`JSR $C253` on the palette buffer
  `$0300..$04FF`) are undone after player 2's pass; player 2 returns beside
  player 1 after 3 s. Player 1's death is the stage's, as in X2.
* **Camera:** follows player 1; player 2 is held within the screen and
  brought back beside player 1 after 1.5 s off it vertically.
* **HUD:** player 2's meter from X2's own HUD tiles (`$80/$82/$84`, badge
  `$86`, OBJ palette 2): Horizontal beside player 1's, Compact below it for
  Zero (X1's layouts); Zero's badge is X3's Z.

Not done: player 2 as X beside a Zero player 1 (not drawn), item pickups
and enemy aiming for player 2, doors/scenes beyond holding player 2 during
player 1's door walk.

Status (xx0.0.8alpha): **exchange art, sounds and layering.**

* Zero is no longer painted over the finished frame. Each frame he (with his
  KSX planes, saber effects and the exchange beam) is drawn into one 256x224
  BGR555 canvas handed to the PPU as a host OBJ (`PpuSetExtraObjects`) at X's
  own OAM priority and slot, so BG tiles with priority over X's sprites cover
  him (verified on Wire Sponge's white wall), and fades apply.
* X's exchange beam is X2's own beam art (poses `$3C..$42`, X2 sequences
  `$6F..$71`): CHR from the player's pose records at `$05:9609` (pointer
  +$31, data bank `$05`, `$88:C9C3`), which are **6 bytes** in X2 (count,
  24-bit source, VRAM word destination, top bit ends); arrangement from
  `$8D:8000` group 0 whose pieces are **(flags, x, y, tile)** in X2 (X1:
  x, y, tile, flags); colors from live OBJ palette 1.
* Exchange sounds in X2's numbering: X1 `$0F` teleport out -> X2 `$11`
  (`$88:B214`), `$0E` arrival -> `$10`, `$0A` landing -> `$0F` (X2 action
  `$22` handler). X1's `$0F` is X2's death sound. Translated as Zero's code
  queues them (`write_back`).

Status (xx0.0.7alpha): **saber hits X2 enemies; developer mods on X2.**

* X2 runs its shot-vs-enemy collision (`$88:D7AC` loop) on the interpreter,
  so the generated-code contact hooks never fired. Each now has an owner-1
  pre-opcode twin placed after the instruction it adjusts: hitbox
  `$08:D870` (after `LDY $0020,X`), contact class `$08:DAA3`, reaction
  `$08:DACC`, response `$08:DAD7`, damage `$08:DB05` (after `SBC $F3A4,Y`,
  recomputed with Zero's value).
* KSX arms its hitbox windows only from X1's per-frame host hook (main.c
  `MmxBeforeFrame` -> `MmxZeroSetCollisionRom`), which X2 never runs; X2's
  frame_begin now makes that call on X1's cart. Without it every saber object
  carried pointer 0 (no box). Boxes are copied into X2's cart every player
  end and on each hitbox query; any shot tagged `$53xx/$56xx/$5A53` counts.
* Verified on Robot Junkyard: KSX slash 1 and 2 hit a scrap robot (class
  `$11`), damage 3 each (X2 native 8 overridden), one hit per swing.
* Hitbox overlay draws on the X2 page (X1's collector on the view, X2's cart,
  KSX box looked up at `$86:FE00`). Tier 2 diagnostics sets X2's
  `tier2_capture` (coverage `tier2_x2_*`); replays record X2 too (xx0.0.17alpha).
* Labels: Hitbox overlay and Tier 2 diagnostics `xx1.0.1`, authors with
  "; X2 support by KamiSquad_X".

Status (xx0.0.6alpha): **ready for playtest.** Add Zero, KSX Zero settings
and Solo Team select run on the X2 page (labels: X3 Zero xx0.0.2 "by: mstan;
X2 support by KamiSquad_X", KSX Zero settings xx0.0.2, Solo Team select
xx0.0.2). Added in this round:

* Enemy contact in X2's generated code (`$08:DA9B..DB09`, X1 `$84:9E15..9E73`):
  contact class (KSX skills as weapon classes), reaction (`$1F2F`, X1
  `$1F1D`), response and damage from X2's enemy table `$86:F3A4` (X1
  `$86:EF37`). The projectile X2 keeps at `$1F30` is translated into the view.
* Saber hitboxes: Zero's records live in X1's cart, bank `$86`
  (`$FF40..$FF67`, `$FFB0..$FFFF`, KSX box `$FA44`). The hitbox hook copies
  them into X2's cart padding (X2 `$86:FD3A..$FFFF` is free) at the same
  addresses, with the KSX box at `$86:FE00` (X2's `$FA44` is data).
* Animations: `kX2ToX1Sequence` maps X2's player sequences (group `$00`,
  `$2F:A222`) to X1's (group `$18`, `$AF:A210`) by identical (timer, pose)
  records; X2-only sequences fall back to X's pose number.
* Sound: X2's command ring is `$09A2` with index `$09D3` (X1 `$0B72`/`$0BA3`),
  now in the view. Saber clips use the host mixer and always played.
* Both health pools refill when an X2 stage enters gameplay (start, respawn).
* Add Axl's body hooks are paused while X2 runs (Axl is not ported).
* The exchange keeps X2's frozen X visible during his morph frames.

Untested by script (the scripted walks never reached an enemy): saber and
skill damage on X2 enemies, R2 buster tiers and burst, each KSX skill.
Known limits: X2 damage-table rows are X2's weapons, so a KSX skill counted
as an X1 weapon index hits as the X2 weapon at that index; Zero's buster
shots start from X's muzzle; Zero draws in front of foreground scenery;
X's beam is invisible during the exchange; developer mods not ported.

Status (xx0.0.5alpha): **Zero's own frame code runs in X2.** Hooks in X2's
generated player code (`tools/apply_x2_zero_hooks.py`: `$08:9479` pre-player,
`$08:947E` player end, `$08:D86D` shot hitbox) and an owner-1 interpreter hook
at `$00:D0AA` (player-shot loop `$80:D0A2`, slots `$10D8..$1318`) run Zero's
X1 code on the X1-layout view. KSX saber swings (ground, running, air) start
from Y and draw through the saber module's overlay and world-sprite providers;
Y no longer fires X's buster. Untested by script: saber damage on X2 enemies,
R2 buster tiers and burst, KSX skills. Not hooked yet: X2's animation-start
sequence numbers (they differ from X1's; Zero's pose follows X's +$17 frame
instead), contact class / damage / response, muzzles, sounds.

View mapping in use: player `$09D8`->`$0BA8`; `$0AE8..$12D8`->`$0C38..$1428`
(helpers, effects, enemies `$0D18`, shots `$10D8`); enemy shots
`$1318`->`$1428`; game mode X2 `$D0..$D3` -> X1 `$D1..$D4`; joypad copies
X2 `$A8..$AF` -> X1 `$A7..$AE`; globals table in `mmx_x2zero.c`.

Earlier (xx0.0.4alpha): **exchange and drawing work.** In X2 stages Select
swaps X and Zero with Zero's own exchange code (teleport timing, separate
health pools, game held still), X2 starts each session as X, and Zero is drawn
in his X3 body art with X3's Z badge on the health bar
(`src/x2ext/mmx_x2zero.c`). Zero still uses X2's native moves and buster:
his own buster burst, saber and KSX skills are next. Mod labels stay
unchanged until those work.

Known limits of the drawing: Zero is painted over the finished PPU frame, so
he shows in front of foreground layers that would cover X; X's side of the
exchange has no teleport art (only Zero's exists in the asset cache).

## What has to move

In X1, Zero is not a separate character. He is X's player object with:

| Part | X1 implementation | Size |
| --- | --- | --- |
| Behavior | `src/mmx_zero.c`, `src/saber/*` read and write X1's player struct and globals | about 250 distinct X1 RAM addresses |
| Native hooks | `tools/apply_zero_hooks.py` and `apply_saber_rock_hooks.py` patch X1's generated code | 55 X1 code addresses |
| Drawing | X1's custom renderer (`src/mmx_renderer.c`) swaps X's sprite pieces for X3 Zero art | renderer is X1-only |
| Select exchange / team | `src/mmx_coop.c` (shared with co-op) | about 190 X1 RAM addresses, 64 code addresses |

X2 runs through the generic PPU and mostly through the interpreter, so the
X1 renderer and X1's generated-code hooks do not reach it.

## Measured so far

Method: X1's player action table (`$81:82A6`, 36 entries) and X2's
(`$88:9634`, 48 entries) were found by `LDX $02 / JMP (table,X)`. Each X1
handler and the routines it calls were disassembled beside X2's and aligned
by instruction shape; operands at aligned instructions give the maps below
(scratch tools: `dis816.py`, `fieldmap.py`, `pcmap.py`).

* **Player struct:** X2 `$09D8` (X1 `$0BA8`). Every direct-page field offset
  maps to itself (+02 action, +05 X, +08 Y, +37/+3B input, +59 fire command,
  ... all identity), so a fixed base delta of `-$1D0` covers the struct.
* **Actions:** `$00`-`$2A` are the same numbers in both games (stand, walk,
  jump, fall, land, death `$0C`, hurt `$0E`, wall `$10`/`$12`, dash `$14`,
  door walks `$18`-`$1E`, `$20`, ladders `$24`-`$2A`). X1 `$2C`+ (ride armor
  and scripted poses) differ and are not mapped yet.
* **Globals (X1 -> X2):** upgrades `$1F99`->`$1FD0`; `$1F0D`->`$1F1E`;
  task pauses `$1F13..$1F19`->`$1F25..$1F2B`; `$1F23`->`$1F36`;
  `$1F31`->`$1F45`; `$1F7A`->`$1FBF`; `$1F86`->`$1FB9`; camera
  `$1E4D`->`$1E5D`, `$1E72/74/76`->`$1E82/84/86`; player helper objects
  `$0C38/$0C58/$0C78`->`$0AE8/$0B08/$0B28`.
* **Hook sites found (X1 -> X2):** `$81:9793`->`$88:BB65` (upgrade read),
  `$81:9EAC`->`$88:C423` and `$81:9EF3`->`$88:C49E` (head probes),
  `$81:A062`->`$88:C52E`, `$81:94AF`->`$88:B837` (shot select),
  `$81:8AE8`->`$88:A6D3` (death orb), `$81:898E`->`$88:9CEC` and
  `$81:8965`->`$88:9CC3` (dash exits), contact/damage `$84:8EEA/8F07/9C16/9E15`
  -> `$88:C673/C690/D86D/D79A`. The rest (player tick `$81:815C/8165`,
  upgrade reads `$81:971C/98FC/9EA5`, weapon costs, muzzles, origins) still
  need manual matching; several sit where the disassembler's M/X guess fails.
* **Player update:** X1 `$81:812E` = X2 `$88:9428` (same prologue; X2 mixes
  extra input words before dispatch). Zero's per-frame entry (X1 `$81:815C`,
  before `JSR (table,X)`) is X2 `$88:9479`; the after-dispatch end (X1
  `$81:8165`) is X2 `$88:947E`. Another global: `$1F0C` -> `$1F1D`.
* **More sites by translated byte search (`hooksearch.py`):**
  respawn `$80:9DA6`->`$80:9D81`; projectile tick `$80:D3E5`->`$80:D10F`;
  helmet/upgrade read `$84:99A4`->`$88:D601`; damage `$84:9D50`/`$84:9F59`
  -> `$88:D9E0`/`$88:DC0B`; contact `$84:9E3A/9E45/9E73` ->
  `$88:DAC9/DAD4/DB02`; `$84:91DB`->`$88:CD7B`; `$87:D0D9`->`$81:CE11`.
  The dash-upgrade reads (`$81:971C/9793/98FC`) need no X2 counterpart:
  X2's X dashes from the start.
* **Shots:** X2's fire routine `$88:B837` matches X1's `$81:94AF` plus X2's
  own weapon cases. Shot-class table `$AF22` (X1 `$BA76`), weapon energy
  `$1FB8,X` (X1 `$1F85,X`). Shot objects are **12 x 32 bytes at
  `$0B48..$0CC8`** (X1: 8 x 64 at `$1228..$1428`), so Zero's own shot
  objects need X2's 32-byte object layout, not just an address change.
* **Object loops (stride, end) in bank $00:** X1 (32,$0C98) (32,$0E18)
  (64,$1228) (64,$1428) (64,$1628) (48,$1928) (32,$1D08); X2 (32,$0B48)
  (32,$0CC8) (64,$10D8) (64,$1318) (64,$1518) (48,$1818) (32,$1A18)
  (32,$1D18) — X2's enemy/item ranges are still to be labeled.
* **X2's player code is AOT:** bank `$08` has 561 generated entries, so Zero's
  X2 hooks must be injected into X2's generated code (an X2 counterpart of
  `tools/apply_zero_hooks.py` run after `regen_project.py`), not only as
  interpreter pre-opcode hooks.
* Only 7 of the 55 X1 hook sites exist byte-for-byte in X2's ROM.
* X2's camera X is `$1E5D` (X1 `$1E4D`).
* Game modes: X2 stage = `$D0`=2, `$D1`=4, gameplay `$D2`=4.

## Porting approach

Zero's code addresses X1 RAM directly (`r[0xbaa]`, `$1F99`, ...). Rather than
rewrite ~250 sites, run it on X2 through an X1-layout view: before each Zero
callback copy X2's player struct and the mapped globals into their X1
addresses of a scratch 128 KiB image, call the unchanged code, and copy the
mapped ranges back. Still open: X2's shot/effect object ranges, X2's
animation sequence numbers for X (Zero's pose adapter keys on X1's), the fire
command class table (X1 `$86:BA76`), sound IDs, and drawing.

## Milestones

1. **Map.** X2 player fields, action numbers, input and fire-command fields,
   shot/effect object ranges, upgrade and HP bytes, and the X2 code sites
   matching each X1 hook.
2. **Drawing.** Zero drawn in X2: hide X's OAM pieces and draw Zero's pose
   over the PPU frame (first version: above backgrounds; correct layer
   priority after).
3. **Body and swap.** Select exchange in X2 stages with Solo Team select
   (X start, Zero partner), Zero's movement, dash and collision.
4. **Buster and saber** (Add Zero), then **KSX Zero** skills and settings.
5. Labels: Add Zero `by: mstan; X2 support by KamiSquad_X`, X3 Zero
   `xx0.0.2`, KSX Zero settings `xx0.0.2`, Solo Team select `xx0.0.2`;
   developer mods (hitbox overlay, Tier 2) `xx1.0.1` if ported.

Dumps used for the table above: an X2 Overdrive Ostrich stage, scripted
idle, walk, jump, dash, shot and charge, `MMX_X2EXT_WATCH` and script `dump`.
