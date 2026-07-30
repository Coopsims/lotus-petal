# The application

Everything here lives under `firmware/main/app/` and knows nothing about the
hardware beyond [`petal_hal.h`](../firmware/main/hal/petal_hal.h).

## The input model

Three gestures, and one rule that makes them safe together:

| Gesture | Does |
|---|---|
| **Turn the dial** | Changes the value the current screen is about. |
| **Tap** | Selects, presses, opens. |
| **Swipe sideways** | Changes screen. |
| **Hold the middle ~0.7 s** | Opens the Tools pie. |

**Taps never change a value.** That is deliberate and it is the reason the whole
thing is usable at a table: a swipe between screens starts as a touch somewhere on
the face, and if taps adjusted numbers, half of them would nudge someone's life
total on the way past.

Telling a tap from a swipe is not free, because the reference touch controller's
own gesture reporting cannot be trusted. So `app_main.c` watches the touch device
directly and measures press-to-release displacement:

- more than `PETAL_SWIPE_MIN_PX` (55) horizontally, and more horizontal than
  vertical → a swipe;
- within `PETAL_TAP_SLOP_PX` (22) in both axes → a tap.

A swipe travels far enough that LVGL never reports `CLICKED` on ordinary tap
zones, so most screens need no help. Screens with a *full-face* tap target are the
exception — a swipe both starts and ends on that object, so LVGL does report a
click — and those call `app_touch_release_was_tap()` to reject it.

Only **two** screens sit in the swipe cycle: **Life** and **Counters**. Everything
else is an overlay pushed on top (`ui/nav.c`), which is precisely why nothing
important can be reached by an accidental swipe. On an overlay, a right-swipe pops
one level.

## Screens

### Life — the home screen

`ui/screens/screen_life.c`, the largest file in the project.

A large life total, starting at `GAME_STARTING_LIFE` (40), inside a 270° arc that
shortens as life falls and sweeps hue 120→0, green through yellow to red. The arc
opens downward, and the gap is where the Pass button sits.

**Above the starting total the gauge laps.** Every 40 points fills the ring once and
then starts again from the beginning in a new colour scheme:

| Life | Ring |
|---|---|
| 1–40 | green → red by level, the original gauge untouched |
| 41–80 | fills again as a blue → purple gradient |
| 81+ | fills again as the full spectrum |

Lapping rather than layering is what keeps this cheap. There is only ever **one band
on the track**: nothing to keep concentric with anything else, no z-order, and the
ring is never painted only to be covered up. The spectrum is a third colour scheme,
not a third layer.

An LVGL arc is a single solid colour, so a lap that needs a gradient cannot be the
widget's own indicator. Those laps are drawn directly onto the arc's layer from a
`LV_EVENT_DRAW_POST` callback on it, with the widget's indicator hidden while they
run; geometry is read off that same arc, so it lines up by construction. Lap 0 is
still just the plain arc.

Three details make the later laps feel like lap 0 rather than a bolt-on:

- **One slice per point of life**, derived from the starting total, so a detent
  advances the band by exactly one slice.
- **The boundary slice is trimmed** to the exact end angle, computed the same way the
  arc computes its own, so length is continuous rather than quantised — and every
  slice is rounded, so the end carries the same cap at any length, growing or
  shrinking.
- **Nothing is stored or repainted speculatively.** Colours are computed inside the
  draw loop, and a detent costs one invalidation of one object — the same as lap 0.
  The spectrum is deliberately static: rotating it cost a full-ring redraw several
  times a second for as long as a player stayed that high.

`LIFE_OVER_FROM_START` flips which end the later laps fill from, that being taste.

Around it:

- **Battery** at the top: a glyph plus a percentage, red below 15 %. While
  charging it animates a filling battery instead, because the reference board
  cannot read the cell's level while plugged in.
- **Turn readout** above the number. In a linked game it becomes the prompt for
  whatever the table is waiting on — `ROLL D20`, `WAITING…`, `TIE - ROLL AGAIN`,
  `PICK SEAT`, `SEATING…` — and is tappable when it is your move to make. It goes
  gold when the turn is yours.
- **Seat label** (`P2`) in a linked game, gold on your turn.
- **Poison**, shown only once you have any, green until 10 and then red.
- **A skull button**, low-left, which appears only at a lethal threshold. Tapping
  it bows you out; tapping again undoes that.
- **Pass**, in the arc's gap. Locally it just advances the turn. In a linked game it
  hands the turn to the next seat.
- **The rim**, in a linked game: one small 270° gauge per other player, laid out by
  turn position so the circle on your screen matches the circle of people at the
  table. The active player's label is gold. A seat belonging to someone without a
  dial still appears, with a dash where their life would be, because leaving them
  out would misrepresent the table.

The colour of the life number resolves in priority order: muted if you are out, red
at 5 or below, gold if the turn is yours, otherwise plain.

Life changes on the dial only. Tapping the face opens commander damage; holding the
middle opens Tools.

### Commander damage

`screen_commander.c`. One wedge per seat — including your own, because a commander
can damage its own controller and that has to be recordable somewhere. Each wedge
runs 0–21 and turns red at 21.

**It is linked to life.** Dialling an opponent's commander damage up takes your life
down by the same amount; dialling it back restores it. Hitting the 0 or 21 clamp
does not move life, so you cannot lose life by pushing against a limit.

Reached by tapping the Life face, not by swiping.

### Counters

`screen_counters.c`. A scrollable two-column grid, entirely data-driven: the table
at the top of the file is the screen. Add a `counter_t` and the tiles, the input
routing and the persistence all resize themselves.

What ships: Treasure, Food, Clue, Blood (the four "tokens", whose tiles go gold
when you hold any), **Cmdr A** and **Cmdr B** (which track times cast and display
the derived +2-per-cast tax rather than the raw count), Poison (red at 10), Energy,
Experience, Storm (zeroed every turn), **Monarch** and **Initiative**, the Ring
(cycles through I–IV), and Day/Night.

**Monarch and Initiative are table-wide.** The rules give each to exactly one
player, so in a linked game they are shared round state rather than a local toggle:
claiming one takes it from whoever had it, and their dial turns off by itself on the
next tick. When someone else holds it your tile shows *their* position (`P3`) rather
than a bare `OFF`, so the crown is always locatable. In a local game there is no
table to share with and both stay the plain toggles they have always been.

Nothing keys off a counter's display name. Each carries its meaning in `role` and
`per_turn`, because matching on labels meant renaming one could silently change the
rules — poison feeds a lethal threshold, so a renamed label would have quietly
stopped the bow-out prompt ever appearing.

Tap a tile to select it, dial to change it. Toggles and cycles advance on the dial
too.

### Tools

`screen_tools.c`. A four-wedge radial menu, opened by holding the middle of the
Life face:

| Wedge | |
|---|---|
| **Undo** | Step the turn counter back one. |
| **Settings** | The settings list. |
| **Reset** | New game — asks to confirm first. |
| **Dice** | The randomiser. |

Spin the dial to move the highlight, tap the centre to open what is highlighted, or
tap a wedge directly.

### Dice

`screen_dice.c`. Laid out for a round face: die types ring the bezel, the result
sits in the middle. Coin, d4, d6, d8, d10, d12, d20, d100, and **dN** — where the
dial sets the side count and the centre rolls, because a knob is the obvious way to
dial in a number.

Spin the dial and the faces tumble while the knob moves, then settle about 200 ms
after it stops. Every tick draws a fresh face from `petal_random()`, so what lands
has nothing to do with what was showing when you let go — you cannot time your
release to rig it.

Opened from Tools, or from the Life screen's `ROLL D20` prompt with the d20 already
selected and the result wired to the table's roll-off.

### Settings

`screen_settings.c`. A scrollable list; the dial scrolls it.

- **Brightness** — a slider, live while dragging, saved on release.
- **Calibrate Touch** — the crosshair flow below.
- **Link Petals** — pair without starting a game, so firmware can be moved between
  dials without setting a table up first.
- **Firmware** — the transfer screen.
- **About** — the mark, the version, the author, and the device's real address,
  chip, build date and SDK version, all read from the platform rather than
  compiled in.

### Calibrate Touch

`screen_calibrate.c`. Five crosshairs — north, east, south, west, then the centre,
derived from the panel size so they stay inside the glass on any face. Tap each
one; a least-squares fit produces a scale and offset per axis.

A wild fit is rejected rather than saved: a sane panel maps roughly 1:1, so a scale
outside 0.5–2.0 means a mis-tap, and saving it would leave the screen unusable and
the calibration screen unreachable.

### The opening flow

1. **Splash** (`screen_splash.c`) — the lotus mark for two seconds, drawn from
   polylines generated at runtime rather than shipped as a bitmap: a few hundred
   bytes instead of a decoded image, and crisp at any size.
2. **Then either**: a game that was genuinely mid-flight is restored silently and
   you land back at the table. "Genuinely" means saved life above 0 and not still
   at 40 — a finished game or an untouched one does not count.
3. **Or**: **Local or Remote** (`screen_mode.c`). Local asks how many players
   (`screen_setup.c`, 2–8) and starts. Remote goes to pairing.

### Remote play flow

4. **Pair** (`screen_pair.c`) — create a game, which shows a random four-digit PIN,
   or join one by dialling the PIN in. Both paths land in the same "linked" view,
   because the link is hostless: the PIN is just the group's name and creating
   versus joining differ only in who picked the number.
5. **Roll for first** — everyone rolls a d20 from the Life screen's prompt. Highest
   goes first; a tie at the top clears every roll and the table rolls again.
6. **Pick seats** (`screen_order.c`) — everyone else claims a turn position from
   what is free. Two dials skip this: whoever did not win is second.
7. **Play** — the rim shows the table, the turn passes with Pass.
8. **Result** (`screen_result.c`) — `VICTORY` or `DEFEATED` when the round ends,
   with **New Round**, which clears the game but keeps everyone linked and seated.

### Firmware transfer

`screen_update.c`. Shows either direction of a dial-to-dial transfer with a
progress bar, and opens itself when an offer arrives — an incoming firmware install
is not something to do silently, and it needs a yes. See
[NETWORKING.md](NETWORKING.md#firmware-transfer).

## The model

### `counter.c` — one struct for everything countable

```c
typedef struct {
    const char *name;
    counter_type_t type;          /* INT, TOGGLE, or CYCLE */
    int value, min, max;
    bool wrap;                    /* wrap at the bounds instead of clamping */
    const char *const *labels;    /* indexed by (value - min); NULL means numeric */
} counter_t;
```

Life, commander damage, poison, the Ring, Day/Night — all of them. Screens only
ever call `counter_increment`, `counter_decrement`, `counter_at_min/max` and
`counter_value_text`, which is why a brand-new counter is a table entry and not a
code change.

### `game.c` — the rules

Turn number, player count (2–8), local versus linked mode, elimination, and the
whole of turn-order resolution.

Three ideas worth calling out:

**Elimination is never automatic.** Life dips below zero constantly mid-resolution,
so reaching a lethal threshold only *offers* the choice — `game_can_eliminate()`
returns true at 0 life, 21 commander damage from one source, or 10 poison, and the
skull button appears. The player decides when they are actually dead.

**The table can be larger than the number of dials.** Not everyone owns one. Table
size is its own shared setting rather than a head count, and a position nobody has
claimed belongs to a player without a dial — which is why `game_pass_turn()` lets
*any* dial pass a turn belonging to an unclaimed seat. Otherwise nobody at the
table could move it on.

**A result is only declared when every seat has a dial.** A player without one has
no way to tell us they are out, so claiming a victory would be a guess.
`game_result()` returns `NONE` unless the dial count equals the table size.

Turn order is resolved identically on every dial from the same inputs, so nobody
has to agree with anybody:

- **First player**: sort every dial's d20 descending, ties broken by seat. Once
  every dial has rolled, the winner — and only the winner — writes the result, so
  the epoch cannot race. A tie at the top clears all rolls.
- **Seating**: each dial claims its own position. If two claim the same one, the
  lower seat keeps it and the other picks again. Both dials compute that rule
  independently and reach the same answer.
- **One special case**: if someone at the table has no dial, they may well have
  rolled higher on real dice and we would never know — so winning the electronic
  roll-off does not seat you first. Everyone picks a position instead, and first
  place is simply left empty if it belongs to a dial-less player.

### `persist.c` — the saved game

The whole in-progress game — life, turn, player count, per-opponent commander
damage, every counter and token — is one fixed-layout blob in storage.

Saving is dirty-flag driven: mutations call `persist_mark_dirty()`, and a 2-second
timer calls `persist_flush()`, which writes only when something changed. Storage
also skips the physical write for an unchanged value, so flash wear stays low even
during a fast game.

The blob is guarded twice: storage refuses a read at the wrong size, and the blob
carries a magic and a version. Change the struct, bump `PERSIST_VERSION`, and old
saves are ignored cleanly rather than misread.

Brightness and the touch map are saved separately, in their own namespaces, by the
HAL — they are settings, not game state, and should survive a New Game.

## Visual language

From `ui/ui_common.h`. Deliberately small — a dark face, one accent, one danger
colour, one highlight:

| | | |
|---|---|---|
| `UI_COL_BG` | `#0A0A0F` | the face |
| `UI_COL_TILE` | `#16161F` | raised surfaces, buttons, inactive arcs |
| `UI_COL_TEXT` | `#F5F5F7` | primary text |
| `UI_COL_MUTED` | `#6B6B78` | secondary text, inactive states |
| `UI_COL_ACCENT` | `#8B5CF6` | selection, and the click flash |
| `UI_COL_DANGER` | `#EF4444` | lethal thresholds |
| `UI_COL_GOLD` | `#F4C430` | your turn, monarch, day, held tokens |

Shared helpers: `ui_make_round_screen()` (a screen styled for the face),
`ui_make_tap_zone()` (transparent clickable overlay that lets swipes bubble
through), `ui_make_flash_overlay()` + `ui_flash()` (the tactile pulse on a value
change), and `ui_make_back_button()`.

`UI_DIM` comes from `PETAL_DISP_W`, so the app never repeats the panel size.
