# Networking

Two protocols share one radio, and neither knows what radio it is. Both are built
only on what [`petal_hal.h`](../firmware/main/hal/petal_hal.h) offers: broadcast
these bytes, send these bytes to that address, tell me when bytes arrive. Packets
may be lost, duplicated or reordered, and both protocols are designed around that
rather than against it.

| | |
|---|---|
| **The game link** — `app/net/net_link.c` | Who is at the table, what their life is, whose turn it is. |
| **The firmware transfer** — `app/net/fw_push.c` | Moving a firmware image from one dial to another. |

The radio offers a single receive hook, so `net_link` takes it and forwards anything
that is not a game packet to a "raw sink" — which is how the firmware transfer hears
its own traffic without needing a second hook.

## The game link

### It is hostless on purpose

There is no master. Every dial runs the same code and reaches the same conclusions
from the same inputs, because the alternative — electing a host — means handling the
host walking away mid-game, which is a whole class of bug nobody at a card table
wants to debug.

Four ideas do all the work:

**A game is a four-digit PIN.** One dial generates a random one ("create"), the
others are told it ("join"). That is the *only* difference between the two roles;
afterwards both behave identically. The PIN is the group's name, not a credential.

**Every dial is the sole writer of its own state.** Life, alive-or-not, its d20
roll, its claimed seat. Nobody else ever writes those, so there are no write
conflicts, no locking, and no merge logic. A missed packet is corrected by the next
heartbeat.

**Membership is discovered, not configured.** Anyone broadcasting the same PIN is a
member. Seats come from sorting every member's hardware address, so all dials
independently compute the *same* seat order with no negotiation — and the table
resizes by itself as dials join or drop.

**Shared state carries an epoch.** Some facts belong to the table rather than to any
one dial, so they cannot be single-writer: the turn pointer, and who holds the
monarchy or the initiative. Every change bumps a counter, and a dial adopts any
state with a higher one, so the newest claim wins and everyone converges on it.

That last part is why monarch and initiative are stored as **which turn position
holds them** rather than as a flag on each dial. A per-dial flag lets four dials all
claim the crown at once with nothing able to settle it — strictly worse than not
tracking it. As a position, the illegal state simply cannot be represented.

### Timing

| | |
|---|---|
| Heartbeat | 700 ms — re-broadcast own state at least this often |
| Expiry | 6 s of silence and a member is dropped |
| Tick | 250 ms, from an LVGL timer |
| Channel | Fixed (channel 1 on the reference radio) |

State is also broadcast immediately on any change, so the heartbeat is a
convergence floor, not the update rate.

### Wire format

34 bytes, packed, little-endian:

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;       /* 0x314E544C, "LTN1" — ours or someone else's  */
    uint8_t  type;        /* 1 = STATE, 2 = LEAVE                          */
    uint8_t  ver;         /* 4                                             */
    uint16_t pin;         /* which table                                   */
    uint8_t  mac[6];      /* who is speaking                               */
    int16_t  life;        /* ── everything above here is FROZEN ──          */
    uint8_t  alive;
    uint8_t  active_pos;  /* turn position being played, 1..players; 0 = not started */
    uint8_t  players;     /* table size, including anyone without a dial    */
    uint8_t  roll;        /* this dial's d20; 0 = has not rolled            */
    uint8_t  order;       /* claimed turn position, 1 = first; 0 = unclaimed */
    uint8_t  monarch;     /* turn position holding the monarchy, 0 = nobody  */
    uint8_t  gen;         /* bumped to start a fresh round                  */
    uint8_t  initiative;  /* turn position holding the initiative            */
    uint8_t  _pad[2];
    uint32_t turn;        /* a full lap of the table                        */
    uint32_t epoch;       /* highest wins, for the shared fields            */
} net_pkt_t;
```

### The compatibility rule

This one is worth taking seriously, because it is what stops two dials on different
builds from going silent at each other.

- **Fields up to and including `alive` are frozen.** Never reorder them, never
  remove them. Only append after.
- A receiver accepts any length from that core up to the full struct, zero-filling
  what the sender did not send. Shorter means an older dial; longer means a newer
  one with fields we do not understand.
- So **life and who is alive always cross versions.** You can always see the table.
- **Round state is only taken from an exactly matching `ver`.** Seating and turn
  semantics have changed before, and a mismatched reading of them is worse than no
  reading — you would show a confidently wrong turn order.

`ver 4` added `monarch` and `initiative`, reusing two padding bytes so the packet
stayed 34 bytes. A ver-3 dial and a ver-4 dial therefore still show each other's
life, but not turn order — the intended trade, and the reason both dials in a pod
want updating together (which the firmware transfer makes about a minute's work).

### Deriving the table

Seats are the rank of each member's address in ascending order across all members.
Every dial computes this locally and gets the same answer, so there is nothing to
agree on. From that, `(turn - 1) % members` gives a shared "active seat" for free.

Note that seats and *turn positions* are different things, and the player only ever
sees the latter. A seat is an internal address ordering; a turn position is what
someone claimed on the seat picker, and it is what the rim, the seat label and the
commander wedges all show.

### The receive path

```
radio driver's context          LVGL thread (net_pump, 250 ms)
─────────────────────────       ──────────────────────────────
radio_rx()                      net_link_tick()
  magic matches?                  drain the queue
    → queue it                    filter: wrong PIN, or our own echo
  otherwise                       LEAVE      → free the slot
    → raw_sink() (firmware)       STATE      → update the member
                                  epoch > ours → adopt shared round state
                                expire anyone silent for 6 s
                                heartbeat if we have been quiet
```

The member table is written from one thread only, which is why every screen can read
it without synchronising.

## Firmware transfer

### Why it exists

Updating a dial normally means finding a cable. This is the alternative: two dials
that are already talking to run a game can carry a firmware image over the same
link. Flash one over USB, and it hands the image to the rest.

It also sidesteps a class of network problem entirely — a guest network that
isolates its clients, or a table with no WiFi at all — because it never touches a
network. There is no access point and no server involved.

### The protocol

Deliberately plain stop-and-wait: one chunk in flight, acknowledged before the next
is sent. That caps throughput at roughly one chunk per round trip, which still moves
a typical image in well under a minute, and it means a dropped packet costs one
retry rather than a resync.

```
sender                                    receiver
──────                                    ────────
OFFER (total bytes)  ─────────────────→   is this from a linked dial?
   ↑ repeats every 1.5 s, up to 40×       ask the player: accept or decline
   │                                            │
   │                    ←───────────────  READY │ (accepted: image slot open)
   │                    ←───────────────  ABORT │ (declined)
   │
DATA seq=0 (≤236 B)  ─────────────────→   seq == expected? write it, expected++
                     ←───────────────── ACK next-expected
DATA seq=1           ─────────────────→
                     ←───────────────── ACK
   …                                     (up to 8 retries per chunk)
END                  ─────────────────→   verify the image, set it to boot next
                     ←───────────────── DONE
                                          reboot into it
```

| | |
|---|---|
| Chunk | 236 bytes of payload; header + chunk fits one 250-byte datagram |
| ACK timeout | 400 ms |
| Retries | 8 consecutive failures before declaring the link lost |
| Offer window | 40 attempts at 1.5 s ≈ 60 s for a human to reach the button |
| Header magic | `0x31574C46`, "FLW1" |

Two details that matter more than they look:

**The receiver always ACKs with what it wants next**, not with what it just got. A
lost ACK therefore self-heals: the sender resends the same chunk, the receiver
re-asks for the same index, and nothing has to resynchronise.

**The offer window is sized for a person, not a packet.** An earlier version retried
for six seconds, which ran out long before anyone could reach the other dial.

### Authorisation

Installing firmware takes **both** of:

1. the sender must be a dial you are **currently linked with** (`net_link_is_member`
   checks the address against the live member table), and
2. you must **accept the offer** on the receiving dial.

Neither is optional, because there is no image signature to fall back on. Without
both, anything in radio range could install its own firmware. The offer screen opens
by itself when an offer arrives — this is not something to do silently — and the
offer expires after 6 seconds of the sender going quiet, so a stale prompt cannot
trap you or block a later offer.

That is the honest security model: **pairing is the authentication.** Signed images
would be better, and are the obvious next step for anyone shipping these to people
who are not the author.

### Safety

The device cannot be bricked by a bad transfer:

- The image is written to the **inactive** app slot, never over the running one.
- `petal_ota_finish()` **verifies before committing**. A transfer that lost bytes is
  rejected, not installed.
- Rollback is enabled, so a new image that never reaches
  `petal_ota_mark_valid()` — which the app calls only once the UI is actually up —
  is reverted by the bootloader on the next boot.

Worth deliberately testing once on a new port: flash something that crashes during
bring-up and confirm the device comes back on the old image.

### Threading

The transfer runs on its own tasks so it cannot stall the UI:

- **`fwrx`** drains the inbound queue and writes flash. Reboots the device when the
  image is complete.
- **`fwtx`** exists only while sending, and spends minutes in the stop-and-wait
  loop.

Neither draws anything. They publish a state, a percentage and a short detail
string, which the update screen polls every 300 ms.

## Testing without a table

- **One dial**: pairing shows a PIN and one seat. `fw_push_available()` is false, so
  the transfer button says "link a petal first".
- **Two dials**: create on one, join on the other; both should show two seats within
  a second or two. Change life on one and watch the other's rim.
- **Drop test**: power one off mid-game. The other should drop it from the rim after
  about six seconds and resize the table.
- **Version test**: build one dial with `NET_VER` bumped and confirm life still
  crosses while round state does not.
- **If they never see each other**: it is almost always the channel — either a
  mismatch, or an access-point association the radio is following. The reference
  `hal_radio.c` disconnects deliberately for exactly this reason.
