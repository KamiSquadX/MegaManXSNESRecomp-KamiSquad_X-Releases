# Independent cameras for online co-op

Online co-op defaults to **Unified** cameras, retaining the shared camera and
horizontal separation limit. In the netplay mod settings, the host can choose
**X / Zero Co-op → Netplay cameras → Independent (Experimental)** for each peer
to see its assigned session seat. Both peers adopt this choice through the room's mod agreement.
The setting appears only in netplay. Offline single-player and couch co-op
always use Unified, regardless of the stored option value.

Owner playtesting of 2.1.0-alpha reported garbled player/platform graphics and
camera shaking near Storm Eagle's moving platforms. Independent remains an
experimental opt-in while these reports are investigated (`beads-8wg.1.95`).
Viewport culling/activation is a possible contributor, not an established cause.
Keeping every spawned actor alive indefinitely could exhaust native actor pools
and change stage behavior, so no lifetime or culling adjustment is bundled with
the default change.

A living player transported out for a scripted scene immediately watches the
scene owner's camera, including departure and arrival. The existing native
scene runs once. The partner returns beside its owner with personal stats
preserved. A fallen player watches the survivor and is never revived by scene
transport. A voluntarily withdrawn player also watches the remaining player.

The reference is Sonic 2's campaign viewport implementation in
`../segagenesisrecomp/SonicTheHedgehog2Recomp`, particularly
`game/sonic2_runtime.c`, `game/sonic2_video.c`, and its
`docs/CAMPAIGN-VIEWPORT-PROTOTYPE.md`. Both games retain one deterministic
simulation and select a local view only for presentation.

## Ownership and rendering

The `cameras` choice lives in the co-op manifest and is part of the host's
mod plan/fingerprint. Plugin activation applies the agreed value on both
peers. The game-local launcher wrapper hides it offline and validates it
before an online launch. Neither the local seat nor a local display setting
can opt a single peer into a different simulation policy.

`src/mmx_coop_view.c` derives the non-anchor view from that player's body,
clamped to authored camera limits. The native camera remains the world
anchor's camera and drives shared scripts. On scene handoff both views use
that native camera immediately; there is no separate camera convergence wait
before placing the returning partner.

The bounds are `$1E56` minimum X, `$1E58` maximum X, `$1E5A` minimum Y and
`$1E5C` maximum Y. The native origin is `$1E4D/$1E50`. A second view follows
body X minus 128 and body Y minus 160. Online motion retains stage bounds and
native pit death, rather than the couch co-op 224-pixel tether.

`MmxBeforeFrame` enables independent policy only when `snes_netplay_active()`,
co-op activation and the agreed `cameras=independent` option are all true.
`snes_netplay_local_slot()` selects the renderer's seat.
The local controller/device index is not the session seat and is not used
for this decision. Builds without netplay select the shared view.

`src/mmx_renderer.c` reprojects world pieces, both bodies, teleport beams,
weapon effects and fallback OAM, keeping HUD coordinates anchored. Online
views use the pre-OAM expanded queues so an object outside the anchor's
hardware screen can still appear on the other peer. Foreground tiles come
from the complete ROM-backed map rather than circular VRAM history. BG2
follows the original `$00:DF08` modes: half/half, full/full, half/full and
full/half. Script/actor surfaces retain their shared scene coordinates.
Spark's light windows and Launch's water plane account for the local origin.
Rendering does not write WRAM or select a simulation actor.

## Shared world activation and native AI

Native spawn records remain authoritative. `$F800` is the 32-pixel-column
pointer table (256 columns plus the terminal pointer at `$FA00`). Flag
entries begin at `$FA02` and contain a live byte, Y word and ROM descriptor
word; descriptors in bank `$85` contain type, Y, ID, subtype and X.

After `$00:DC36` finishes its ordinary scan at `$DCD7`, an online rescan calls
the original `$00:DCDB` allocator for records in either player's rectangle.
The hook at `$DD2D` filters individual records. Mechanisms and boss encounter
controllers use native entry timing; ordinary enemies use spawn hysteresis
and the agreed aspect margin. The scan does not allocate the space between
two distant views. Allocation flags, object pools and collected-item flags
remain native.

A rollback-owned visit bitmap prevents a defeated enemy from being allocated
again during a stationary rescan or the anchor's later native scan. Leaving
both activation windows rearms an ordinary cleared record. Native collected
flags remain intact. The original pool capacities still apply; this change
does not increase enemy, projectile or item slot counts.

The common lifetime/visibility helpers at `$82:806E`, `$82:808F` and
`$82:80B4`, plus Ride Armor's private `$83:8948` cull, test the union of
complete rectangles. They cannot combine one player's X range with the
other player's Y range. This keeps distant actors alive without activating
the empty gap between players.

### Boss and miniboss encounters

An encounter's intro seizes the native camera, which is the world actor's
(the anchor). A boss/miniboss record (`MmxWidePolicy_IsScriptedEncounter`)
therefore spawns only inside the anchor's view. When the partner's view
reaches one first, `encounter_handoff` (`mmx_coop.c`) passes the world to
him before it wakes, through the door handoff's
`MmxCoopViewsAdoptPlayer`, so the fight runs around the player who is there.
Before this, the partner woke Bee Blader far ahead of P1: the lock scrolled
P1's screen across the stage toward the arena, and P2 was clamped back into
P1's screen.

During an encounter the arena's invisible walls (its native, script-set
camera limits) hold only a player inside it: the anchor, plus a partner
standing between those walls on the anchor's screen. A partner outside, even
just outside, keeps his own region bounds, so the walls never stop him walking
in; one step past the wall line and they hold him too. Highway's Vile (enemy
`$32`) counts as an arena: before, he was missing from the list, neither
player had walls, and Vile could push X out past the arena edge.

### Scripts that move the native camera

Besides encounters, two scripts move the native camera when a player reaches
them, and so dragged the anchor's screen when the partner got there first:

- Highway's airship event (kind 2 record `$0E` at x=6848) locks the camera to
  the deployment strip and sends the airship (`$1A`). Woken for the partner,
  it waited on the anchor's camera: the airship never came and the partner
  stood at an invisible wall (6912+248 = 7160, his region's right edge). It is
  now a world encounter (`MmxViewWorldEncounter`, `mmx_rtl.c`): only the
  anchor's view wakes it, and the partner reaching it first takes the world.
- Kuwanger's main elevator (`$3D`) sets the camera targets to its shaft as it
  starts. Ridden by the partner alone, it panned the anchor's camera from the
  stage start across to the shaft: the anchor was pushed along the camera edge
  into a pit, the elevator stalled and the partner's screen went black.
  `camera_object_handoff` passes the world to the partner as he reaches the
  elevator, before boarding sets those targets (handing over one frame after
  boarding replaced them and stalled it).

The anchor's own clamp (`constrain_player`) now uses the native limits
(`player_limits` skips the anchor): Highway's airship widens `$1E60` to 7424,
which region history never sees, and the anchor stopped mid-screen at 7160.
The anchor's tracked bounds follow the native targets every frame, so a
handoff gives him back the room he was in. When the anchor dies and the other
player takes the world (Normal death), the survivor adopts his own view
(`MmxCoopViewsAdoptSeatView`); left on the dead anchor's camera, the native
clamp pushed X from the Kuwanger start to x=1288 over a pit.

### Scenes and boss doors

A scene (door, boss intro, capsule) belongs to the player who is there. A
partner on or just off the anchor's screen is still beamed along with him. A
partner further away is not pulled in: `scene_stay` keeps him where he stands,
paused with his own camera, until the scene ends.

A boss door the partner reaches on his own opens only for him: he steps
through to the far side (`door_cross`), and the anchor keeps the native door,
camera and any fight beyond it. As natively, a door never opens from inside.

### Independent netplay cameras settings

X / Zero / Axl Co-op 0.0.2 has a second feature, **Independent netplay
cameras settings**. It has no switch of its own: it follows
`megaman-x.coop-axl:coop:cameras=independent` (`follows_any` with an
`option=value` condition), so it is on exactly while the room uses
Independent cameras. Its options travel with the host's mod plan.

- **Select button teleports to other player** (off by default). A tap of
  Select (released within 24 frames) beams to the other player's screen, also
  from that screen itself. From off it, the landing is the edge the player
  came from; on it, beside the other player at his ground level. Tapping again
  before moving (any other button, leaving the ground or being knocked ends
  this) cycles the landing: left edge, right edge, his level, then exactly
  where he stands, and round again. A choice with no place is skipped. Every
  landing must be on the route: `on_route` walks from it to the other player
  over terrain with steps no taller than 32 px, so a pit behind a pillar (where
  X once landed and was trapped) never qualifies. His own footing always does,
  terrain or object (Bee Blader's wreck, the crumbling road).
  Holding Select for five seconds (`select_long`, 2-frame units) beams the
  player to the stage's start as entered from stage select: checkpoint 0's
  record (`$80:E68E`, bank `$86` table at `$A780`: x at +3, the beam's y at +5,
  room limits at +$0F..+$15), with that room's camera limits.
  Only while standing or walking on the ground, not attacking (shot/saber
  button held or a swing running), hurt, collecting, in a capsule or script,
  and outside a boss fight or its arena walls. The world pauses during the
  beam, so it cannot be hurt. A teleporting anchor hands the world to the
  other player first. With this rule on, holding Select no longer withdraws.
  State: `teleport_cycle` (a nibble per seat) and `select_long[2]`, both in
  bytes that were reserved, so the co-op save layout is unchanged.
- **Death-falls behavior**: Normal (Teleports is listed, not yet available).
- **Death behavior**: Normal (the other player continues) or Shared: a death
  sends the team to the checkpoint as a single-player death does. A dying
  partner takes the world and its camera first. Respawn is listed, not yet
  available.

### Highway's crumbling road

The road pieces after the second Bee Blader (item `$09`) only test the
projected body. With independent views they update against the nearest
living player (`pickup_hook`, `$00:D2E6/D308`), so they fall under whoever
walks on them; before, they stayed solid under a partner far from the anchor.

### Background planes

The native view is rebuilt from the stage map too while views are online:
an encounter or door handoff moves the native camera across the stage in one
frame, and the game only streams new tilemap columns at the screen edges, so
circular VRAM kept the old area (the dark shapes at Bee Blader after a
handoff). Highway's switch to the arena sky at BG2 `$A00` is judged from each
view's own BG2 origin; judged from the native camera it smeared one column
across a far-behind view. Each background phase lists only the CHR and colors
it changes; a distant view takes what its phase does not reload from the
phases before it (stages whose phases run left to right), not from the
native section's live VRAM/CGRAM.

### Art for actors outside the native section

OBJ resources are loaded by section (`$1F08`), and the section follows the
native camera. Actors spawned for a view sections ahead never had their
resource bound: their attribute byte `+$11` has no palette, page or priority
(only the flip bits the AI sets), so their priority-0 pieces sat behind the
background, which made them invisible but still dangerous (a turning road
attacker had `+$11 = $40`). With independent views, the renderer
draws such actors with their resource's ROM art and priority. It also
searches the stage's other sections for an animation the current section
does not list.

Native enemy and enemy-projectile updates temporarily project the nearest
living player's body for AI. Enemy loops `$00:D4EA/$D507` and projectile
loops `$00:D48D/$D4AA` restore the caller's actor at their balanced loop
continuations. Contact retries remember the first projected seat and test
the other seat, preserving the native result and actual Slimer capture owner.
Shared scene/menu updates retain their existing owner.

Imported X2/X3 projectiles query their creator's derived simulation view for
viewport lifetimes and screen effects. They never use the local peer seat.
`tools/apply_coop_hooks.py` routes compiled routine boundaries through the
existing interpreter hook path only when required; uncompiled native helpers
already use that path.

## Save and rollback state

MMX extra state version 16 appends `MmxCoopViewWorldState` after the existing
co-op contexts. It contains the spawn visit bitmap, stage initialization and
balanced actor/contact continuation ownership. Older snapshots load with an
empty bitmap, which is rebuilt from native live flags on the next scan.

The online-session gate, local view seat and presentation offsets are excluded
from simulation state. Both peers simulate the same regions and receive the
same actors regardless of which view they display. Reset and stage entry
clear the visit history.

## Validation

Focused checks use owned ROMs/assets and private scratch directories:

- `MMX_COOP_VIEWS_BOOT_TEST=1`: retail boot into Highway; P1 waits at X 128
  while P2 reaches X 742. Both retain 16 HP. Native enemies update visibly
  outside P1's activation rectangle. Enemy AI, spawning and shots replay from
  a snapshot, and a cleared remote enemy is not reallocated while both views
  remain stationary.
- `MMX_COOP_VIEWS_TEST=1`: separated movement, distinct rendered views,
  read-only presentation, horizontal/vertical rectangle policy, snapshot
  replay, and actual departure/return from a distance with either scene owner.
  Camera handoff occurs on the first departure frame; HP survives the return.
- `tools/test_netplay_pair.py --independent-views`: two actual local UDP/lobby
  peers, 480 frames, separation beyond 250 pixels, matching positions and HP,
  and a delayed input change causing rollback on both peers. Each peer saves
  its own rendered view. The test uses isolated installations and ports.
- Existing renderer unit checks and Slimer ownership/replay checks pass.

Highway captures were visually reviewed for both rosters. Vertical window
policy is checked separately; this is not a full vertical-stage or campaign
qualification. Boss doors, capsules, room scrolls, unusual stage mechanisms
and two-machine internet play remain important owner playtest targets.
