// Exponential backoff with jitter for reconnect attempts.
// Pure C++, no Arduino dependency, so it is unit-tested on the host.
#pragma once
#include <stdint.h>

class Backoff {
 public:
  // rng: returns a pseudo-random uint32; injectable so tests are deterministic.
  Backoff(uint32_t base_ms, uint32_t cap_ms, uint8_t jitter_pct, uint32_t (*rng)());

  // Delay to wait before the next attempt, then advances the attempt counter.
  // Sequence without jitter: base, 2*base, 4*base, ... capped at cap_ms.
  uint32_t next();

  // Call after a successful connection so the next failure starts from base_ms.
  void reset();

  uint32_t attempts() const { return attempts_; }

 private:
  uint32_t base_ms_;
  uint32_t cap_ms_;
  uint8_t jitter_pct_;
  uint32_t (*rng_)();
  uint32_t attempts_ = 0;
};
