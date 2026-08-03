#pragma once
// Fuhr multitronic lock bus - pure protocol logic.
//
// Deliberately free of any ESPHome dependency so it can be compiled and
// unit-tested on a PC (see tests/test_native.cpp).
//
// Line format (measured on 30 logic captures, 330 frames):
//   2000 baud, idle = not asserted, start bit = asserted.
//   Logical bit = NOT asserted level (inverted UART).
//   1 start bit + 32 line bits + 1 stop bit = 34 bit times = 17.0 ms.
//   The 32 line bits are Manchester coded: 10 = 1, 01 = 0, giving 16 payload
//   bits = 8 bit message code + CRC-8 (poly 0x1D, init 0x00, no reflection).
//   An acknowledge is a start bit plus a single logical 0, sent by the other
//   party 17.4 ms after every data frame.

#include <stdint.h>
#include <stddef.h>

namespace fuhr {

static const uint8_t MAX_FRAME_EDGES = 48;
static const uint8_t FRAME_QUEUE_LEN = 8;

// ---------------------------------------------------------------- CRC-8
inline uint8_t crc8(uint8_t byte) {
  uint8_t crc = byte;
  for (uint8_t i = 0; i < 8; i++)
    crc = (crc & 0x80) ? (uint8_t) ((crc << 1) ^ 0x1D) : (uint8_t) (crc << 1);
  return crc;
}

// ------------------------------------------------------------ message codes
enum Sender : uint8_t { SENDER_LOCK = 0, SENDER_CTRL = 1, SENDER_UNKNOWN = 2 };

struct MessageInfo {
  uint8_t code;
  Sender sender;
  const char *name;
};

// Codes 0x00-0x0F are lock status, 0x10-0x1F commands and handshake.
static const MessageInfo MESSAGES[] = {
    {0x02, SENDER_LOCK, "Door opened"},
    {0x03, SENDER_LOCK, "Door closed"},
    {0x05, SENDER_LOCK, "Locking started"},
    {0x06, SENDER_LOCK, "Bolts retracted"},
    {0x07, SENDER_LOCK, "Door locked"},
    {0x08, SENDER_LOCK, "Day bolt retracted"},
    {0x0D, SENDER_LOCK, "Fault / alarm"},
    {0x0E, SENDER_LOCK, "Locking finished"},
    {0x10, SENDER_CTRL, "Cmd: open (day mode active)"},
    {0x11, SENDER_CTRL, "Cmd: day mode on, day bolt retracted once"},
    {0x12, SENDER_CTRL, "Cmd: unlock"},
    {0x13, SENDER_CTRL, "Cmd: day mode off"},
    {0x14, SENDER_CTRL, "Cmd: retract day bolt"},
    {0x15, SENDER_LOCK, "Manually unlocked"},
    {0x16, SENDER_LOCK, "Action complete: unlocked"},
    {0x17, SENDER_CTRL, "Cmd: day mode on"},
    {0x18, SENDER_LOCK, "Door opened while locked"},
    {0x19, SENDER_LOCK, "Command acknowledged"},
};
static const size_t MESSAGE_COUNT = sizeof(MESSAGES) / sizeof(MESSAGES[0]);

inline const MessageInfo *lookup(uint8_t code) {
  for (size_t i = 0; i < MESSAGE_COUNT; i++)
    if (MESSAGES[i].code == code)
      return &MESSAGES[i];
  return nullptr;
}

inline const char *message_name(uint8_t code) {
  const MessageInfo *m = lookup(code);
  return m ? m->name : "Unknown code";
}

// The sender is deterministic per code, so direction can be recovered even
// without wiring up the controller-TX line.
inline Sender message_sender(uint8_t code) {
  const MessageInfo *m = lookup(code);
  return m ? m->sender : SENDER_UNKNOWN;
}

// ------------------------------------------------------------------ frames
struct DecodedFrame {
  uint32_t start_us;
  bool is_ack;
  bool crc_ok;
  bool manchester_ok;
  uint8_t code;
  uint8_t crc;
  uint32_t line_bits;   // the raw 32 line bits
  Sender sender;        // from the ctrl line if available, else from the code
  bool sender_measured; // true when it came from the ctrl line
};

// Edge driven frame decoder.
//
// Feed it every edge of the bus line; call poll() regularly so a frame that
// ends in silence can time out. Completed frames are queued and retrieved
// with pop().
class FrameDecoder {
 public:
  void set_bit_time_us(uint32_t us) { bit_us_ = us; }
  uint32_t bit_time_us() const { return bit_us_; }

  void reset() {
    frame_active_ = false;
    edge_count_ = 0;
    q_head_ = q_tail_ = 0;
    dropped_ = 0;
  }

  // asserted: bus is at the active (start bit) level after this edge.
  // ctrl: state of the controller-TX line, or false when not wired.
  void on_edge(uint32_t t_us, bool asserted, bool ctrl, bool ctrl_valid) {
    if (frame_active_) {
      // Segment on edge timestamps rather than on when we happen to run, so
      // this stays correct even if the main loop stalls for milliseconds.
      //
      // A data frame occupies 34 bit times, so anything asserted after that
      // starts the next frame. An acknowledge is only ~2 bit times long and
      // the lock may answer as little as 6 ms later, so a short burst
      // followed by an idle gap also ends the frame. Manchester coding
      // guarantees a data frame keeps producing edges until bit 32, so a
      // burst this short can only be an acknowledge.
      if (asserted) {
        bool after_frame = (int32_t) (t_us - (start_us_ + 34 * bit_us_)) >= 0;
        bool short_burst = (last_edge_us_ - start_us_) <= 4 * bit_us_ &&
                           (t_us - last_edge_us_) >= 2 * bit_us_;
        if (after_frame || short_burst) {
          finish_();
          begin_(t_us, ctrl, ctrl_valid);
          return;
        }
      }
      if (edge_count_ < MAX_FRAME_EDGES) {
        edges_[edge_count_].t = t_us;
        edges_[edge_count_].asserted = asserted;
        edge_count_++;
      }
      last_edge_us_ = t_us;
      return;
    }
    if (asserted)
      begin_(t_us, ctrl, ctrl_valid);
  }

  // Call from the main loop; finishes a frame once the line has been quiet
  // long enough that no further edges can belong to it.
  void poll(uint32_t now_us) {
    if (frame_active_ && (int32_t) (now_us - (start_us_ + 35 * bit_us_)) >= 0)
      finish_();
  }

  bool pop(DecodedFrame *out) {
    if (q_head_ == q_tail_)
      return false;
    *out = queue_[q_tail_];
    q_tail_ = (uint8_t) ((q_tail_ + 1) % FRAME_QUEUE_LEN);
    return true;
  }

  uint32_t dropped() const { return dropped_; }

 protected:
  struct FrameEdge {
    uint32_t t;
    bool asserted;
  };

  void begin_(uint32_t t_us, bool ctrl, bool ctrl_valid) {
    frame_active_ = true;
    start_us_ = t_us;
    last_edge_us_ = t_us;
    ctrl_at_start_ = ctrl;
    ctrl_valid_ = ctrl_valid;
    edge_count_ = 0;
  }

  // Level of the bus at an absolute time, reconstructed from the edge list.
  // Before the first recorded edge the line is at the start bit level.
  bool asserted_at_(uint32_t t) const {
    bool lvl = true;
    for (uint8_t i = 0; i < edge_count_; i++) {
      if ((int32_t) (edges_[i].t - t) > 0)
        break;
      lvl = edges_[i].asserted;
    }
    return lvl;
  }

  // Logical bit k, sampled in the middle of its bit time. The line is
  // inverted, so a logical 1 is the idle (not asserted) level.
  uint8_t bit_at_(uint8_t k) const {
    uint32_t t = start_us_ + ((2u * k + 3u) * bit_us_) / 2u;
    return asserted_at_(t) ? 0 : 1;
  }

  void push_(const DecodedFrame &f) {
    uint8_t next = (uint8_t) ((q_head_ + 1) % FRAME_QUEUE_LEN);
    if (next == q_tail_) {
      dropped_++;
      return;
    }
    queue_[q_head_] = f;
    q_head_ = next;
  }

  void finish_() {
    frame_active_ = false;

    DecodedFrame f;
    f.start_us = start_us_;
    f.is_ack = false;
    f.crc_ok = false;
    f.manchester_ok = false;
    f.code = 0;
    f.crc = 0;
    f.line_bits = 0;
    f.sender = SENDER_UNKNOWN;
    f.sender_measured = ctrl_valid_;

    uint8_t bits[32];
    for (uint8_t k = 0; k < 32; k++) {
      bits[k] = bit_at_(k);
      // An acknowledge is a start bit plus one logical 0. A data frame always
      // continues in 01/10 pairs, so two 1s here can only be an ACK.
      if (k == 3 && bits[2] == 1 && bits[3] == 1) {
        f.is_ack = true;
        break;
      }
    }

    if (f.is_ack) {
      if (ctrl_valid_)
        f.sender = ctrl_at_start_ ? SENDER_CTRL : SENDER_LOCK;
      push_(f);
      return;
    }

    uint16_t payload = 0;
    bool ok = true;
    for (uint8_t i = 0; i < 32; i += 2) {
      f.line_bits = (f.line_bits << 2) | (uint32_t) (bits[i] << 1) | bits[i + 1];
      if (bits[i] == 1 && bits[i + 1] == 0)
        payload = (uint16_t) ((payload << 1) | 1);
      else if (bits[i] == 0 && bits[i + 1] == 1)
        payload = (uint16_t) (payload << 1);
      else {
        ok = false;
        payload = (uint16_t) (payload << 1);
      }
    }
    f.manchester_ok = ok;
    f.code = (uint8_t) (payload >> 8);
    f.crc = (uint8_t) (payload & 0xFF);
    f.crc_ok = ok && (crc8(f.code) == f.crc);
    if (ctrl_valid_)
      f.sender = ctrl_at_start_ ? SENDER_CTRL : SENDER_LOCK;
    else if (f.crc_ok)
      f.sender = message_sender(f.code);
    push_(f);
  }

  uint32_t bit_us_{500};
  bool frame_active_{false};
  uint32_t start_us_{0};
  uint32_t last_edge_us_{0};
  bool ctrl_at_start_{false};
  bool ctrl_valid_{false};
  FrameEdge edges_[MAX_FRAME_EDGES];
  uint8_t edge_count_{0};
  DecodedFrame queue_[FRAME_QUEUE_LEN];
  uint8_t q_head_{0}, q_tail_{0};
  uint32_t dropped_{0};
};

// ------------------------------------------------------------- lock state
enum DoorState : uint8_t { DOOR_UNKNOWN = 0, DOOR_OPEN, DOOR_CLOSED };
enum LockState : uint8_t { LOCK_UNKNOWN = 0, LOCK_LOCKED, LOCK_UNLOCKED, LOCK_LOCKING };
enum DayMode : uint8_t {
  DAY_UNKNOWN = 0,
  DAY_OFF,
  DAY_ON_BOLT_RETRACTED,
  DAY_ON_BOLT_OUT,
};
// Tri-state, so an entity can stay unpublished until the bus tells us.
enum Tri : uint8_t { TRI_UNKNOWN = 0, TRI_FALSE, TRI_TRUE };

// Derives the four status values from the message stream.
//
// The mapping is documented in README.md; it follows directly from the
// sequences observed in the 25 captures.
struct StateTracker {
  DoorState door{DOOR_UNKNOWN};
  LockState lock{LOCK_UNKNOWN};
  DayMode day{DAY_UNKNOWN};
  Tri fault{TRI_UNKNOWN};   // 0x0D
  Tri tamper{TRI_UNKNOWN};  // 0x18

  void reset() {
    door = DOOR_UNKNOWN;
    lock = LOCK_UNKNOWN;
    day = DAY_UNKNOWN;
    fault = TRI_UNKNOWN;
    tamper = TRI_UNKNOWN;
    day_active_ = false;
    day_known_ = false;
    bolt_retracted_ = false;
    last_command_ = 0;
  }

  void apply(uint8_t code) {
    if (message_sender(code) == SENDER_CTRL)
      last_command_ = code;

    // Any message the lock sends other than 0x0D means it is running
    // normally, so the fault resolves on the next ordinary status. This is
    // also what lifts the entity out of "unknown" shortly after boot: the
    // lock repeats its status every ~15 s.
    if (message_sender(code) == SENDER_LOCK)
      fault = (code == 0x0D) ? TRI_TRUE : TRI_FALSE;

    switch (code) {
      case 0x02:  // door opened
        door = DOOR_OPEN;
        if (lock == LOCK_UNKNOWN) // update lock status, when door is opened lock is unlocked. 
          lock = LOCK_UNLOCKED;
        bolt_retracted_ = false;
        break;
      case 0x03:  // door closed, not locked
        door = DOOR_CLOSED;
        lock = LOCK_UNLOCKED;
        bolt_retracted_ = false;
        break;
      case 0x05:  // locking started
        lock = LOCK_LOCKING;
        break;
      case 0x06:  // bolts retracted
        lock = LOCK_UNLOCKED;
        break;
      case 0x07:  // door locked
        door = DOOR_CLOSED;
        lock = LOCK_LOCKED;
        day_active_ = false;
        day_known_ = true;
        bolt_retracted_ = false;
        break;
      case 0x08:  // day bolt retracted
        bolt_retracted_ = true;
        break;
      case 0x11:  // cmd day mode on, day bolt retracted once (jumper TGF)
        day_active_ = true;
        day_known_ = true;
        break;
      case 0x0D:  // fault / blocked - handled by the rule above
        break;
      case 0x0E:  // locking finished
        lock = LOCK_LOCKED;
        day_active_ = false;
        day_known_ = true;
        bolt_retracted_ = false;
        break;
      case 0x13:  // cmd day mode off
        day_active_ = false;
        day_known_ = true;
        break;
      case 0x15:  // manually unlocked
        lock = LOCK_UNLOCKED;
        break;
      case 0x16:  // action complete: unlocked
        lock = LOCK_UNLOCKED;
        // With jumper TGF the day bolt is retracted only briefly and comes
        // back out; 0x16 marks the end of that window. After 0x12 (loop
        // function 9+10) the day bolt stays retracted, so only clear it for
        // the 0x11 variant.
        if (last_command_ == 0x11)
          bolt_retracted_ = false;
        // The tamper state has no status message of its own to end it: while
        // it lasts the lock reports neither the door opening nor closing. It
        // is only lifted once the lock is commanded to unlock, and 0x16 is
        // what reports that having completed.
        tamper = TRI_FALSE;
        break;
      case 0x17:  // cmd day mode on
        day_active_ = true;
        day_known_ = true;
        break;
      case 0x18:  // door opened while locked
        door = DOOR_OPEN;
        tamper = TRI_TRUE;
        break;
      default:
        break;  // 0x10, 0x12, 0x14, 0x19 carry no state by themselves
    }
    if (!day_known_)
      day = DAY_UNKNOWN;
    else if (!day_active_)
      day = DAY_OFF;
    else
      day = bolt_retracted_ ? DAY_ON_BOLT_RETRACTED : DAY_ON_BOLT_OUT;
  }

  static const char *door_str(DoorState s) {
    switch (s) {
      case DOOR_OPEN: return "Open";
      case DOOR_CLOSED: return "Closed";
      default: return "Unknown";
    }
  }
  static const char *lock_str(LockState s) {
    switch (s) {
      case LOCK_LOCKED: return "Locked";
      case LOCK_UNLOCKED: return "Unlocked";
      case LOCK_LOCKING: return "Locking";
      default: return "Unknown";
    }
  }
  static const char *day_str(DayMode s) {
    switch (s) {
      case DAY_OFF: return "Off";
      case DAY_ON_BOLT_RETRACTED: return "On, day bolt retracted";
      case DAY_ON_BOLT_OUT: return "On, day bolt out";
      default: return "Unknown";
    }
  }
  static const char *tri_str(Tri s) {
    switch (s) {
      case TRI_TRUE: return "Yes";
      case TRI_FALSE: return "No";
      default: return "Unknown";
    }
  }

 protected:
  bool day_active_{false};
  bool day_known_{false};
  bool bolt_retracted_{false};
  uint8_t last_command_{0};
};

}  // namespace fuhr
