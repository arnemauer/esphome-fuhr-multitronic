# esphome-fuhr-multitronic

ESPHome component that listens on the bus between a **Fuhr multitronic**
electronic multi-point lock and its controller, decodes every message, and
exposes the lock's state to Home Assistant.

The component is **receive only**. It never drives the bus, so it cannot
disturb the controller or the lock.

The protocol is not published by the manufacturer. It was reverse engineered
from 30 logic-analyser captures of a working installation; the full
description is in [Protocol](#protocol) below.

---

## Contents

- [Entities](#entities)
- [Wiring](#wiring)
- [Installation](#installation)
- [Configuration](#configuration)
- [Protocol](#protocol)
  - [Physical layer](#physical-layer)
  - [Frame format](#frame-format)
  - [Manchester coding](#manchester-coding)
  - [Checksum](#checksum)
  - [Acknowledge frames](#acknowledge-frames)
  - [Worked example](#worked-example)
- [Communication](#communication)
  - [Message codes](#message-codes)
  - [Command handshake](#command-handshake)
  - [Keep-alive and timeout](#keep-alive-and-timeout)
  - [Typical sequences](#typical-sequences)
  - [The tamper state](#the-tamper-state-ends-only-on-an-unlock-command)
  - [Loop functions](#loop-functions)
- [Terminology](#terminology)
- [How the states are derived](#how-the-states-are-derived)
- [Caveats and open questions](#caveats-and-open-questions)
- [Tests](#tests)
- [Implementation notes](#implementation-notes)

---

## Entities

| Entity | Type | Values |
|---|---|---|
| `message` | text_sensor | every decoded frame, e.g. `LOCK 0x03 Door closed` or `CTRL 0x17 Cmd: day mode on` |
| `door` | text_sensor | `Open` · `Closed` · `Unknown` |
| `lock` | text_sensor | `Locked` · `Unlocked` · `Locking` · `Unknown` |
| `day_mode` | text_sensor | `Off` · `On, day bolt retracted` · `On, day bolt out` · `Unknown` |
| `connected` | binary_sensor | connectivity: off when no message has arrived within `timeout` |
| `fault` | binary_sensor | problem: on at `0x0D`, off at any other message from the lock |
| `tamper` | binary_sensor | tamper: on at `0x18`, off at `0x16` — see [below](#the-tamper-state-ends-only-on-an-unlock-command) |

Everything starts unknown after a reboot and fills in as messages arrive. The
lock repeats its status every ~15 s, so door, lock and fault state are correct
within about 15 seconds of boot. `day_mode` needs a `0x17`/`0x13`
command or a lock cycle before it is known — see
[Caveats](#caveats-and-open-questions).

## Wiring

> **The bus runs at the lock's supply voltage. Do not connect it straight to a
> GPIO.** Use an opto-coupler or a level shifter into a 3.3 V input.

| Signal | Required | Notes |
|---|---|---|
| Bus | yes | must idle **low** at the GPIO and go **high** for the start bit. If your interface inverts, set `inverted: true` on the pin |
| Controller TX | no | the line that is asserted while the controller transmits. Improves direction reporting and lets the component flag a message arriving from the wrong side |

The controller-TX line is genuinely optional: every message code is only ever
sent by one of the two parties, so direction is recovered from the code table
when that line is absent. Both paths are covered by the test suite.

## Installation

```yaml
external_components:
  - source: github://arnemauer/esphome-fuhr-multitronic
    components: [fuhr_multitronic]
```

Or clone the repository and use `type: local` with `path: components`, as in
[`fuhr_lock.yaml`](fuhr_lock.yaml).

## Configuration

```yaml
fuhr_multitronic:
  id: lock_bus
  bus_pin:
    number: GPIO16
    mode: {input: true}
  ctrl_pin:                 # optional
    number: GPIO17
    mode: {input: true}
  baud_rate: 2000           # optional
  timeout: 30s              # optional, drives the `connected` sensor
  publish_acks: false       # optional, acknowledges double the message rate

text_sensor:
  - platform: fuhr_multitronic
    fuhr_multitronic_id: lock_bus
    message: {name: "Lock last message"}
    door: {name: "Door status"}
    lock: {name: "Lock status"}
    day_mode: {name: "Day mode"}

binary_sensor:
  - platform: fuhr_multitronic
    fuhr_multitronic_id: lock_bus
    connected: {name: "Lock connection"}
    fault: {name: "Lock fault"}
    tamper: {name: "Lock tamper alarm"}
```

### `on_message` trigger

```yaml
fuhr_multitronic:
  id: lock_bus
  bus_pin: {number: GPIO16, mode: {input: true}}
  on_message:
    - lambda: |-
        // code: uint8_t, name: std::string, from_controller: bool
        if (code == 0x18) ESP_LOGW("lock", "Tamper alarm!");
```

[`fuhr_lock.yaml`](fuhr_lock.yaml) is a complete working configuration. It also
adds template binary sensors that mirror `door` and `lock` with the matching
Home Assistant device classes; `fault` and `tamper` are already binary.

---

# Protocol

Everything below was derived from 30 logic-analyser captures totalling
**330 data frames**, all of which decode cleanly and pass the checksum.

## Physical layer

| | |
|---|---|
| Bit rate | **2000 baud** (500 µs per bit) |
| Idle level | low |
| Polarity | inverted UART — a physical **high is a logical 0** |
| Topology | one shared line; both parties transmit on it |
| Direction | a second line is asserted while the controller transmits, and idle while the lock transmits |

## Frame format

A data frame is 34 bit times, 17.0 ms in total:

```
      ┌───────┬────────────────────────────────────────────┬──────┐
idle  │ start │            32 line bits                    │ stop │  idle
──────┘       │                                            │      └───────
              └────────────────────┬───────────────────────┘
                                   │ Manchester decode
                                   ▼
                        ┌───────────────┬───────────────┐
                        │  bits 15..8   │   bits 7..0   │   16 payload bits
                        │ message code  │     CRC-8     │
                        └───────────────┴───────────────┘
```

The start bit is a logical 0 (line driven high), the stop bit a logical 1
(line idle). Data is transmitted **MSB first**.

## Manchester coding

Each payload bit is sent as two line bits:

| Payload bit | Line bits |
|---|---|
| `1` | `10` |
| `0` | `01` |

So the 32 "data bits" visible on a logic analyser carry only **16 bits of
information**. A useful tell: every hex nibble of a valid frame is `5`, `6`,
`9` or `A` — those are the only nibbles made of `01`/`10` pairs. Anything else
is a decoding error.

Because only the low 5 bits of the message code are ever used, the real
information content of a frame is 5 bits of message plus 8 bits of checksum.

## Checksum

**CRC-8, polynomial `0x1D`, init `0x00`, no reflection, no final XOR**,
computed over the single message byte:

```c
uint8_t crc8(uint8_t byte) {
  uint8_t crc = byte;
  for (uint8_t i = 0; i < 8; i++)
    crc = (crc & 0x80) ? ((crc << 1) ^ 0x1D) : (crc << 1);
  return crc;
}
```

`0x1D` is the classic SAE-J1850 polynomial, here with a zero init value. The
checksum is therefore purely linear, which is how it was identified:

```
c(0x01) = 0x1D   c(0x02) = 0x3A   c(0x04) = 0x74
c(0x08) = 0xE8   c(0x10) = 0xCD
```

and every observed code is the XOR of those basis values — for example
`c(0x13) = c(0x10) ^ c(0x02) ^ c(0x01) = 0xCD ^ 0x3A ^ 0x1D = 0xEA`.

## Acknowledge frames

Every data frame is answered by the **other** party with a short frame:
one start bit plus a single logical 0, then idle. No payload, no checksum.

* sent **17.4 ms** after the data frame started, i.e. right after its stop bit
* in the reference captures all 330 data frames were acknowledged; none were
  repeated or lost

An acknowledge is only ~2 bit times long, and the next data frame can follow
as little as 6 ms later, so a decoder must not assume a fixed 34 bit-time slot
per frame. (This is exactly the bug the test suite caught during development.)

## Worked example

The most common frame on the bus, `555A596A`:

```
line bits   0101 0101 0101 1010 0101 1001 0110 1010
pairs       01 01 01 01 01 01 10 10 01 01 10 01 01 10 10 10
payload      0  0  0  0  0  0  1  1  0  0  1  0  0  1  1  1   = 0x0327

code = 0x03      "Door closed"     (door closed, not locked)
crc  = 0x27      crc8(0x03) = 0x27  ✓
```

---

# Communication

## Message codes

Codes `0x00`–`0x0F` are lock status, `0x10`–`0x1F` are commands and
handshake. Direction is fixed per code.

### Controller → lock

| Code | Line bits | Meaning |
|---|---|---|
| `0x10` | `5655A5A6` | Open door / day bolt while day mode is active |
| `0x11` | `5656A655` | Day mode **on**, day bolt retracted once — jumper TGF fitted |
| `0x12` | `5659AA6A` | Unlock door (loop function 9 + 10, day mode not active) |
| `0x13` | `565AA999` | Day mode **off** |
| `0x14` | `56659A96` | Retract day bolt / release the request input |
| `0x17` | `566A96A9` | Day mode **on**, day bolt stays out — jumper TGF removed |

### Lock → controller

| Code | Line bits | Meaning |
|---|---|---|
| `0x02` | `55595A99` | Door opened |
| `0x03` | `555A596A` | Door closed, not locked |
| `0x05` | `55666996` | Locking started (motor running) |
| `0x06` | `556965A9` | Bolts retracted / unlocked — also the periodic status in day mode |
| `0x07` | `556A665A` | Locked and secured — also the periodic status in night mode |
| `0x08` | `5595A995` | Day bolt retracted |
| `0x0D` | `55A69556` | Fault / blocked / alarm |
| `0x0E` | `55A99969` | Locking finished, bolts thrown |
| `0x15` | `56669965` | Lock opened manually (key or handle) |
| `0x16` | `5669955A` | Action complete — lock unlocked |
| `0x18` | `56955966` | Door opened while locked — tamper alarm, persists until the lock is commanded to unlock |
| `0x19` | `56965A95` | Command acknowledged |

## Command handshake

```
CTRL  ──► command (0x10 / 0x12 / 0x17)
LOCK  ──► 0x19   command acknowledged        (+ 20 ms)
LOCK  ──► status codes while the mechanism moves
LOCK  ──► 0x16   action complete
CTRL  ──► echoes its own command             (+ 20 ms after 0x16)
```

`0x13` and `0x14` never receive a `0x19`. They behave as "off / release"
commands: after `0x13` the lock either relocks (`0x05` → `0x0E` → `0x07`) or
reports `0x0D` if it is blocked.

Note that `0x19` (command acknowledged) is a normal data frame and is itself
acknowledged by a short ACK frame — the two mechanisms are separate layers.

## Keep-alive and timeout

The lock repeats its current status code every **14.88 s** (measured across 49
intervals, spread 14.82–14.93 s). The controller falls back to a
"door not locked" fault within **21 s** of the last status — roughly one and a
half missed keep-alives. The component's `timeout` option (default 30 s)
drives the `connected` binary sensor on the same principle.

## Typical sequences

**Unlock, open the door, close it, automatic relock.** Timings are from a real
capture; the controller held the request input for 1.0 s.

```
 5.275 s  CTRL → 0x12  Cmd: unlock
 5.299 s  LOCK → 0x19  Command acknowledged
 5.352 s  LOCK → 0x06  Bolts retracted
 6.273 s  CTRL → 0x14  Cmd: retract day bolt      ← input released after 1.0 s
 6.367 s  LOCK → 0x08  Day bolt retracted
 9.089 s  LOCK → 0x02  Door opened
 9.112 s  LOCK → 0x16  Action complete: unlocked
 9.136 s  CTRL → 0x14  (echo)
14.767 s  LOCK → 0x03  Door closed
18.526 s  LOCK → 0x05  Locking started            ← 3.8 s after the door closed
20.849 s  LOCK → 0x0E  Locking finished           ← 2.3 s motor run
20.873 s  LOCK → 0x07  Door locked
```

Two timings are strikingly consistent across every capture: the motor takes
**2.21–2.32 s** from `0x05` to `0x0E`, and `0x0E` is followed by `0x07` within
20 ms. A closed door waits about **3.8 s** before locking starts.

**Switching day mode on:**

```
7.567 s  CTRL → 0x17  Cmd: day mode on
7.591 s  LOCK → 0x19  Command acknowledged
7.644 s  LOCK → 0x06  Bolts retracted
9.204 s  LOCK → 0x16  Action complete: unlocked
9.227 s  CTRL → 0x17  (echo)
   ...   LOCK → 0x06  repeated every ~14.9 s
```

### The tamper state ends only on an unlock command

**Forcing the door while it is locked** — the lock reports the tamper and then
holds `0x18` as its status, replacing the normal keep-alive:

```
 8.538 s  LOCK → 0x07  Door locked
 8.968 s  LOCK → 0x02  Door opened
 8.992 s  LOCK → 0x18  Door opened while locked
 9.015 s  LOCK → 0x06  Bolts retracted
23.965 s  LOCK → 0x18  ← keep-alive, and every ~14.9 s after that
38.894 s  LOCK → 0x18
53.823 s  LOCK → 0x18
```

Two things about this state are worth knowing, because they are the reason the
`tamper` sensor is cleared the way it is:

* **It persists until the lock is commanded to unlock.** Closing the door does
  not end it. `0x16` (action complete: unlocked) is the first message that
  follows the unlock, so that is what clears the sensor.
* **While it lasts, the lock reports neither the door opening nor the door
  closing.** The only thing on the bus is `0x18`, repeated every ~14.9 s. So
  during a tamper the `door` entity keeps whatever it had at `0x18`, which is
  `Open`; do not read it as live door state until the tamper clears.

The practical consequence: treat `tamper` as a latched alarm, not as a live
door-position signal. If you need to know whether the door is actually shut
again during a tamper, this bus cannot tell you — a separate door contact can.

## Loop functions

The commands map onto the lock's documented loop functions:

| Loop function | Activate | Release | Day bolt |
|---|---|---|---|
| 9 + 10, continuous | `0x12` | `0x14` | retracted and **stays** retracted |
| 9 + 10 while day mode is active | `0x10` | — | the documented "afterwards the day bolt can be operated via ports 9 + 10" |
| 11 + 12, jumper TGF **removed** | `0x17` | `0x13` | **stays out** — no `0x08` at all |
| 11 + 12, jumper TGF **fitted** | `0x11` | `0x13` | retracted **once**, comes back out after ~5 s |

The jumper does not just change the lock's behaviour — the controller sends a
different command code for it. `0x11` and `0x17` are otherwise interchangeable:
both switch day mode on, both are released with `0x13`.

Ports 7 + 8 were not captured. According to the manufacturer's documentation
they offer the same two functions; ports 11 + 12 with TGF fitted behave the
same way (day bolt back out after 5 s).

**Function 1 (pulse ≤ 1 s) and function 2 (continuous) use the same command
code.** The difference is only how long the input is held, visible as the gap
before the matching release:

```
1 s pulse       13.93 s C 0x12  →  14.94 s C 0x14   (+1.00 s)
continuous       4.53 s C 0x12  →  67.01 s C 0x14   (+62 s)
```

### Day mode with and without the TGF jumper

The two variants are byte-for-byte identical apart from the command code and
the day bolt handling:

```
TGF removed                          TGF fitted
 9.262 s  CTRL → 0x17                 9.166 s  CTRL → 0x11
 9.286 s  LOCK → 0x19                 9.190 s  LOCK → 0x19
 9.339 s  LOCK → 0x06                 9.244 s  LOCK → 0x06
                                     10.257 s  LOCK → 0x08   ← day bolt retracted
10.888 s  LOCK → 0x16                17.773 s  LOCK → 0x16
10.912 s  CTRL → 0x17  (echo)        17.797 s  CTRL → 0x11  (echo)
   ...    LOCK → 0x06 every ~14.9 s      ...   LOCK → 0x06 every ~14.9 s
31.451 s  CTRL → 0x13                38.246 s  CTRL → 0x13
35.842 s  LOCK → 0x05                42.038 s  LOCK → 0x05
38.060 s  LOCK → 0x0E                44.256 s  LOCK → 0x0E
38.084 s  LOCK → 0x07                44.280 s  LOCK → 0x07
```

Note that `0x16` arrives 1.55 s after `0x06` without the jumper, but 7.5 s
after `0x08` with it. **The day bolt coming back out is not reported
separately** — `0x16` is the only marker for the end of that window. Loop
function 9 + 10 (`0x12`) shows exactly the same `0x08` → 7.5 s → `0x16`
pattern, even though there the day bolt stays retracted, so the timing alone
does not distinguish the two; the activating command does.

---

## Terminology

Everything in this repository is in English. The manufacturer's documentation
and the original capture filenames are Dutch, so here is the mapping:

| Dutch | English |
|---|---|
| slot | lock |
| deur | door |
| dagstand | day mode |
| dagschoot | day bolt (the latch) |
| nachtschoot | night bolt (the deadbolt) |
| haakschoot | hook bolt |
| schoten | bolts |
| ingetrokken | retracted |
| vergrendeld / ontgrendeld | locked / unlocked |
| storing | fault |
| loopfunctie | loop function |
| openingspuls | opening pulse |

---

## How the states are derived

| Code | Meaning | Effect |
|---|---|---|
| `0x02` | Door opened | door = Open, lock = Unlocked, day bolt out |
| `0x03` | Door closed | door = Closed, lock = Unlocked, day bolt out |
| `0x05` | Locking started | lock = Locking |
| `0x06` | Bolts retracted | lock = Unlocked |
| `0x07` | Door locked | door = Closed, lock = Locked, day mode off |
| `0x08` | Day bolt retracted | day bolt retracted |
| `0x0D` | Fault / alarm | fault = on |
| `0x0E` | Locking finished | lock = Locked, day mode off |
| `0x11` | Cmd day mode on (TGF) | day mode on |
| `0x13` | Cmd day mode off | day mode off |
| `0x15` | Manually unlocked | lock = Unlocked |
| `0x16` | Action complete: unlocked | lock = Unlocked, tamper = off; day bolt back out if the last command was `0x11` |
| `0x17` | Cmd day mode on | day mode on |
| `0x18` | Door opened while locked | door = Open, tamper = on |
| `0x10` `0x12` `0x14` `0x19` | — | no state change on their own |

`fault` follows a rule rather than a table row: **any** message the lock sends
other than `0x0D` clears it. That is also what lifts it out of unknown shortly
after boot, since the lock repeats its status every ~15 s. Controller commands
never resolve it — the controller has no idea whether the lock is blocked.

Frames that fail the CRC never update the state. They are logged as a warning
and published to `message` as `ERROR 0x..`.

## Caveats and open questions

* **`day_mode` starts as `Unknown`.** Day mode is tracked from the `0x17`
  (on) and `0x13` (off) commands, plus `0x07`/`0x0E` which imply it is off. It
  is deliberately *not* inferred from `0x06`: that status also appears for loop
  function 9 + 10 (all bolts retracted), which is not day mode. Guessing there
  would silently report day mode when the door is merely being held open.
* **Ports 7 + 8 were never captured.** The installation these captures come
  from does not use them. Per the manufacturer's documentation they offer the
  same two functions as ports 9 + 10, and ports 11 + 12 with TGF fitted behave
  identically (day bolt back out after ~5 s), so `0x11` is likely what 7 + 8
  produces too — but that is inference, not measurement.
* **The day bolt coming back out is not announced.** With `0x11` the component
  reports *On, day bolt retracted* between `0x08` and `0x16`,
  then *On, day bolt out*. That matches the observed ~5 s behaviour closely but is
  derived from `0x16`, not from a dedicated message.
* **Unknown codes are logged, not guessed.** Any code outside the table is
  published as `Unknown code` and leaves the state untouched.

## Tests

```bash
g++ -std=c++11 -O2 -Wall -Wextra -Werror -o tests/test_native tests/test_native.cpp
cd tests && ./test_native .
```

Replays all 30 captures through the real decoder, four times each: from
`t = 0` and with the clock offset so `micros()` wraps mid-capture, each with
and without the controller-TX line connected.

```
captures replayed : 30 (x2 clock bases x2 wiring modes)
data frames       : 330
acknowledges      : 330
failures          : 0
PASS
```

The raw `.sr` captures are not in this repository — the generated fixtures in
`tests/edges/` are committed instead, so the suite runs without them.
`tests/export_edges.py` regenerates the fixtures when new captures are added.

## Implementation notes

`components/fuhr_multitronic/fuhr_protocol.h` contains the entire protocol —
frame decoder, Manchester, CRC-8 and state machine — with no ESPHome
dependency, so it compiles and runs on a PC. `fuhr_multitronic.cpp` only adds
the interrupt handler, a 64-entry edge ring buffer and the entity plumbing.

Frames are segmented on **edge timestamps**, not on when the main loop happens
to run, so a loop stall of tens of milliseconds cannot corrupt a frame. Bits
are sampled at absolute offsets from the start bit rather than by accumulating
bit periods.

## Licence

MIT — see [LICENSE](LICENSE).

Fuhr and multitronic are trademarks of their respective owner. This project is
not affiliated with or endorsed by the manufacturer.
