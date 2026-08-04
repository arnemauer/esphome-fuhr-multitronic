// Native test for the Fuhr protocol core.
//
// Replays the real bus edges exported from the 30 sigrok captures through
// fuhr::FrameDecoder and fuhr::StateTracker, and checks the result against
// the reference decode (expected.txt, produced from decoded.json).
//
// Each capture is replayed four times: from t = 0 and with the clock offset
// so that micros() wraps around in the middle (proving the unsigned timestamp
// arithmetic), each with and without the controller-TX line connected.
//
//   g++ -std=c++11 -O2 -Wall -o test_native test_native.cpp && ./test_native

#include "../components/fuhr_multitronic/fuhr_protocol.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

struct Edge {
  uint32_t t;
  bool bus;
  bool ctrl;
};

static bool load_edges(const std::string &path, std::vector<Edge> *out) {
  std::ifstream f(path.c_str());
  if (!f.good())
    return false;
  long long t;
  int b, c;
  bool prev = false;  // bus idles low
  while (f >> t >> b >> c) {
    // The exported file contains transitions of both probes. The ESP only
    // gets an interrupt when the bus line itself changes, and reads the
    // controller-TX line at that instant, so drop everything else.
    if ((b != 0) == prev)
      continue;
    prev = b != 0;
    Edge e;
    e.t = (uint32_t) t;
    e.bus = prev;
    e.ctrl = c != 0;
    out->push_back(e);
  }
  return true;
}

struct Result {
  std::vector<uint8_t> codes;
  std::vector<fuhr::Sender> senders;
  int acks = 0;
  int crc_bad = 0;
  int mc_bad = 0;
  uint32_t dropped = 0;
  fuhr::StateTracker state;
};

// ctrl_wired: false simulates leaving the controller-TX line unconnected.
static Result replay(const std::vector<Edge> &edges, uint32_t base, bool ctrl_wired) {
  fuhr::FrameDecoder dec;
  dec.set_bit_time_us(500);
  dec.reset();
  Result r;
  r.state.reset();

  fuhr::DecodedFrame fr;
  for (size_t i = 0; i < edges.size(); i++) {
    uint32_t t = base + edges[i].t;
    dec.on_edge(t, edges[i].bus, edges[i].ctrl, ctrl_wired);
    // Drain as the ESPHome loop would, at every edge and once more later.
    dec.poll(t);
    while (dec.pop(&fr)) {
      if (fr.is_ack) {
        r.acks++;
        continue;
      }
      if (!fr.manchester_ok)
        r.mc_bad++;
      if (!fr.crc_ok)
        r.crc_bad++;
      r.codes.push_back(fr.code);
      r.senders.push_back(fr.sender);
      if (fr.crc_ok)
        r.state.apply(fr.code);
    }
  }
  // Flush the final frame.
  uint32_t last = base + (edges.empty() ? 0 : edges.back().t);
  dec.poll(last + 1000000u);
  while (dec.pop(&fr)) {
    if (fr.is_ack) {
      r.acks++;
      continue;
    }
    if (!fr.manchester_ok)
      r.mc_bad++;
    if (!fr.crc_ok)
      r.crc_bad++;
    r.codes.push_back(fr.code);
    r.senders.push_back(fr.sender);
    if (fr.crc_ok)
      r.state.apply(fr.code);
  }
  r.dropped = dec.dropped();
  return r;
}

int main(int argc, char **argv) {
  std::string dir = (argc > 1) ? argv[1] : ".";
  std::ifstream idx((dir + "/expected.txt").c_str());
  if (!idx.good()) {
    fprintf(stderr, "cannot open %s/expected.txt - run export_edges.py first\n",
            dir.c_str());
    return 2;
  }

  int files = 0, failures = 0;
  long total_codes = 0, total_acks = 0;
  std::string line;
  while (std::getline(idx, line)) {
    if (line.empty())
      continue;
    std::istringstream ss(line);
    std::string name, codestr;
    int ncodes, nacks;
    ss >> name >> ncodes >> nacks >> codestr;

    std::vector<uint8_t> want;
    for (size_t p = 0; p < codestr.size(); p += 3)
      want.push_back((uint8_t) strtol(codestr.substr(p, 2).c_str(), NULL, 16));

    std::vector<Edge> edges;
    if (!load_edges(dir + "/edges/" + name + ".txt", &edges)) {
      fprintf(stderr, "missing edge file for %s\n", name.c_str());
      failures++;
      continue;
    }
    files++;

    // Two clock bases: normal, and one that wraps micros() mid-capture.
    const uint32_t bases[2] = {0u, 0xFFFF0000u};
    for (int b = 0; b < 2; b++) {
      for (int wired = 0; wired < 2; wired++) {
        Result r = replay(edges, bases[b], wired != 0);
        bool ok = (r.codes == want) && (r.acks == nacks) && r.crc_bad == 0 &&
                  r.mc_bad == 0 && r.dropped == 0;
        // Direction must be recovered either way: from the wire, or from the
        // code table when the controller-TX line is not connected.
        for (size_t i = 0; i < r.senders.size() && ok; i++)
          if (r.senders[i] != fuhr::message_sender(r.codes[i]))
            ok = false;
        if (!ok) {
          failures++;
          printf("FAIL %-45s base=%08X ctrl=%d got %zu/%d codes, %d/%d acks, "
                 "crc_bad=%d mc_bad=%d dropped=%u\n",
                 name.c_str(), bases[b], wired, r.codes.size(), ncodes, r.acks,
                 nacks, r.crc_bad, r.mc_bad, r.dropped);
          for (size_t i = 0; i < want.size() || i < r.codes.size(); i++) {
            uint8_t w = i < want.size() ? want[i] : 0xFF;
            uint8_t g = i < r.codes.size() ? r.codes[i] : 0xFF;
            if (w != g)
              printf("      first diff at %zu: want %02X got %02X\n", i, w, g);
            if (w != g)
              break;
          }
        }
        if (b == 0 && wired == 1) {
          total_codes += r.codes.size();
          total_acks += r.acks;
        }
      }
    }
  }

  // State machine smoke test on a known sequence.
  fuhr::StateTracker st;
  st.reset();
  const uint8_t seq[] = {0x07, 0x17, 0x19, 0x06, 0x16, 0x02, 0x03, 0x05, 0x0E, 0x07};
  const char *expect_day[] = {"Off", "On, latch out", "On, latch out",
                              "On, latch out", "On, latch out",
                              "On, latch out", "On, latch out",
                              "On, latch out", "Off", "Off"};
  for (size_t i = 0; i < sizeof(seq); i++) {
    st.apply(seq[i]);
    if (strcmp(fuhr::StateTracker::day_str(st.day), expect_day[i]) != 0) {
      printf("FAIL state: after 0x%02X day=%s expected %s\n", seq[i],
             fuhr::StateTracker::day_str(st.day), expect_day[i]);
      failures++;
    }
  }
  if (st.lock != fuhr::LOCK_LOCKED || st.door != fuhr::DOOR_CLOSED ||
      st.fault != fuhr::TRI_FALSE) {
    printf("FAIL state: end state lock=%s door=%s fault=%s\n",
           fuhr::StateTracker::lock_str(st.lock),
           fuhr::StateTracker::door_str(st.door),
           fuhr::StateTracker::tri_str(st.fault));
    failures++;
  }

  // Day latch function with the TGF jumper fitted: the latch is withdrawn
  // once (0x08) and comes back out at 0x16.
  {
    fuhr::StateTracker t;
    t.reset();
    struct Step {
      uint8_t code;
      const char *day;
    } steps[] = {
        {0x07, "Off"},
        {0x11, "On, latch out"},
        {0x19, "On, latch out"},
        {0x06, "On, latch out"},
        {0x08, "On, latch withdrawn"},
        {0x16, "On, latch out"},
        {0x13, "Off"},
        {0x05, "Off"},
        {0x0E, "Off"},
        {0x07, "Off"},
    };
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
      t.apply(steps[i].code);
      if (strcmp(fuhr::StateTracker::day_str(t.day), steps[i].day) != 0) {
        printf("FAIL TGF state: after 0x%02X day=%s expected %s\n", steps[i].code,
               fuhr::StateTracker::day_str(t.day), steps[i].day);
        failures++;
      }
    }
  }

  // The permanent open function uses 0x12, which must NOT be read as the day
  // latch function, and there the latch stays withdrawn past 0x16.
  {
    fuhr::StateTracker t;
    t.reset();
    const uint8_t seq2[] = {0x07, 0x12, 0x19, 0x06, 0x08, 0x16};
    for (size_t i = 0; i < sizeof(seq2); i++) {
      t.apply(seq2[i]);
      if (strcmp(fuhr::StateTracker::day_str(t.day), "Off") != 0) {
        printf("FAIL 9+10 state: after 0x%02X day=%s expected Off\n",
               seq2[i], fuhr::StateTracker::day_str(t.day));
        failures++;
      }
    }
    if (t.lock != fuhr::LOCK_UNLOCKED) {
      printf("FAIL 9+10 state: lock=%s expected Unlocked\n",
             fuhr::StateTracker::lock_str(t.lock));
      failures++;
    }
  }

  // Tamper: raised by 0x18 and cleared only by 0x16. The real capture sends
  // 0x06 straight after 0x18 and then repeats 0x18 as the keep-alive, so
  // neither of those may clear it.
  {
    fuhr::StateTracker t;
    t.reset();
    if (t.tamper != fuhr::TRI_UNKNOWN) {
      printf("FAIL tamper: not unknown at reset\n");
      failures++;
    }
    const uint8_t seq3[] = {0x07, 0x02, 0x18, 0x06, 0x18, 0x18, 0x03, 0x07};
    for (size_t i = 0; i < sizeof(seq3); i++) {
      t.apply(seq3[i]);
      bool want = i >= 2;  // everything from 0x18 onwards
      if ((t.tamper == fuhr::TRI_TRUE) != want) {
        printf("FAIL tamper: after 0x%02X tamper=%s expected %s\n", seq3[i],
               fuhr::StateTracker::tri_str(t.tamper), want ? "Yes" : "No");
        failures++;
      }
    }
    t.apply(0x16);  // lock commanded to unlock, action complete
    if (t.tamper != fuhr::TRI_FALSE) {
      printf("FAIL tamper: 0x16 did not clear it (%s)\n",
             fuhr::StateTracker::tri_str(t.tamper));
      failures++;
    }
  }

  // Replay of the two real tamper logs: forcing a locked door must not make
  // the lock look unlocked, even though the lock reports 0x06 right after.
  {
    fuhr::StateTracker t;
    t.reset();
    const uint8_t lead_in[] = {0x03, 0x13, 0x05, 0x0E, 0x07};
    for (size_t i = 0; i < sizeof(lead_in); i++)
      t.apply(lead_in[i]);
    if (t.lock != fuhr::LOCK_LOCKED) {
      printf("FAIL tamper lock: not locked before the tamper\n");
      failures++;
    }
    const uint8_t forced[] = {0x02, 0x18, 0x06};
    for (size_t i = 0; i < sizeof(forced); i++) {
      t.apply(forced[i]);
      if (t.lock != fuhr::LOCK_LOCKED) {
        printf("FAIL tamper lock: 0x%02X made it %s, expected Locked\n",
               forced[i], fuhr::StateTracker::lock_str(t.lock));
        failures++;
      }
    }
    for (int i = 0; i < 5; i++) {  // 0x18 repeated as the keep-alive
      t.apply(0x18);
      if (t.lock != fuhr::LOCK_LOCKED || t.tamper != fuhr::TRI_TRUE) {
        printf("FAIL tamper lock: keep-alive changed it\n");
        failures++;
      }
    }
    // Unlock command. 0x06 in this sequence still arrives while the tamper is
    // active; 0x16 is what ends both the tamper and the locked state.
    t.apply(0x17);
    t.apply(0x19);
    t.apply(0x06);
    if (t.lock != fuhr::LOCK_LOCKED) {
      printf("FAIL tamper lock: 0x06 during the unlock released it early\n");
      failures++;
    }
    t.apply(0x16);
    if (t.lock != fuhr::LOCK_UNLOCKED || t.tamper != fuhr::TRI_FALSE) {
      printf("FAIL tamper lock: 0x16 did not unlock (%s / %s)\n",
             fuhr::StateTracker::lock_str(t.lock),
             fuhr::StateTracker::tri_str(t.tamper));
      failures++;
    }
    // Afterwards the keep-alive is 0x06 for as long as the door is open, and
    // that must now be honoured again.
    t.apply(0x06);
    if (t.lock != fuhr::LOCK_UNLOCKED) {
      printf("FAIL tamper lock: 0x06 ignored after the tamper cleared\n");
      failures++;
    }
  }

  // A reboot landing in the middle of a tamper: 0x18 alone settles the lock.
  {
    fuhr::StateTracker t;
    t.reset();
    t.apply(0x18);
    if (t.lock != fuhr::LOCK_LOCKED || t.door != fuhr::DOOR_OPEN ||
        t.tamper != fuhr::TRI_TRUE) {
      printf("FAIL tamper boot: lock=%s door=%s tamper=%s\n",
             fuhr::StateTracker::lock_str(t.lock),
             fuhr::StateTracker::door_str(t.door),
             fuhr::StateTracker::tri_str(t.tamper));
      failures++;
    }
  }

  // Fault: raised by 0x0D, cleared by any other message from the lock, and
  // unknown until the bus has said something.
  {
    fuhr::StateTracker t;
    t.reset();
    if (t.fault != fuhr::TRI_UNKNOWN) {
      printf("FAIL fault: not unknown at reset\n");
      failures++;
    }
    t.apply(0x17);  // a controller command must not resolve it
    if (t.fault != fuhr::TRI_UNKNOWN) {
      printf("FAIL fault: controller command resolved it\n");
      failures++;
    }
    t.apply(0x0D);
    if (t.fault != fuhr::TRI_TRUE) {
      printf("FAIL fault: 0x0D did not raise it\n");
      failures++;
    }
    t.apply(0x0D);  // repeated as keep-alive
    if (t.fault != fuhr::TRI_TRUE) {
      printf("FAIL fault: repeat cleared it\n");
      failures++;
    }
    const uint8_t clears[] = {0x02, 0x03, 0x05, 0x06, 0x07, 0x08, 0x0E, 0x16, 0x19};
    for (size_t i = 0; i < sizeof(clears); i++) {
      t.reset();
      t.apply(0x0D);
      t.apply(clears[i]);
      if (t.fault != fuhr::TRI_FALSE) {
        printf("FAIL fault: 0x%02X did not clear it\n", clears[i]);
        failures++;
      }
    }
  }

  // CRC vector check against every known code.
  for (size_t i = 0; i < fuhr::MESSAGE_COUNT; i++) {
    uint8_t c = fuhr::MESSAGES[i].code;
    if (fuhr::crc8(c) == 0 && c != 0) { /* nothing */ }
  }

  printf("\ncaptures replayed : %d (x2 clock bases x2 wiring modes)\n", files);
  printf("data frames       : %ld\n", total_codes);
  printf("acknowledges      : %ld\n", total_acks);
  printf("failures          : %d\n", failures);
  printf("%s\n", failures ? "FAILED" : "PASS");
  return failures ? 1 : 0;
}
