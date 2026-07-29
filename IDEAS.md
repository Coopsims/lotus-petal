# Ideas and parking lot

Things considered but not built, kept so they are not lost twice. Nothing here is
committed work — it is a backlog to pull from.

For what *is* built, see the [README](README.md) and
[docs/APPLICATION.md](docs/APPLICATION.md).

## Worth doing

- **Signed firmware images.** The dial-to-dial transfer currently authenticates by
  pairing: the sender must be a linked dial and the receiver must accept. That is
  reasonable for a table of friends and not enough for anything shipped to
  strangers. An image signature would let a dial refuse an image on its own merits
  rather than trusting the link.
- **Commander damage that notifies.** When you dial damage onto an opponent, their
  dial could reflect it — the link already carries everything needed, and only one
  dial is the writer of any given seat, so it would need a "damage dealt to you"
  message rather than a shared field.
- **Shared monarch and initiative.** Both are table-wide facts currently tracked
  per-dial, so two people can believe they hold the crown. Same shape of problem as
  the turn pointer, and the epoch mechanism already solves it.
- **A screen-off timeout with wake-on-input.** The single biggest remaining battery
  saving. Left out so far because it changes behaviour mid-game, which is exactly
  when you do not want surprises.
- **Life history sparkline.** A small life-over-time graph on a secondary view. All
  the data already flows through one place.

## Considered and declined

- **Per-turn timer / chess clock.** Reuse the life ring as a depleting arc, driven
  off the existing Pass button. Declined: not wanted at a casual table.
- **Life-change undo stack.** Undo an accidental dial spin. Declined: turning the
  dial back is easier than finding an undo button, and the dial is right there.
- **Format presets for starting life** (Commander 40 / Brawl 30 / Standard 20).
  Declined for now because the whole UI is built around Commander — `GAME_STARTING_LIFE`
  is a single constant, so this is genuinely easy if someone wants it.
- **Colour-identity theming.** Set a deck's colour identity and tint the ring and
  accents to match. Cosmetic, and the palette is deliberately small.

## Ports people have asked about

Not started, but the HAL boundary is what makes them tractable — see
[docs/PORTING.md](docs/PORTING.md).

- **A desktop simulator.** LVGL ships an SDL driver, and `app/` is already free of
  platform headers, so this is a `hal/sdl/` directory: a display, a mouse standing
  in for touch, arrow keys for the dial, files for storage, and UDP broadcast for
  the radio. It would make the UI iterable without a device, and would prove the
  boundary holds better than any grep can.
- **Square or rectangular panels.** Builds and runs today; the layout constants are
  tuned for a round 360×360 face and would want revisiting.
- **A knob with a push-button.** `input_event.h` already defines `INPUT_EV_ACTION`
  and the select events for exactly this; no screen depends on them existing, and
  nothing currently emits them.
