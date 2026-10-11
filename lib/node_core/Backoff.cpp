#include "Backoff.h"

Backoff::Backoff(uint32_t base_ms, uint32_t cap_ms, uint8_t jitter_pct, uint32_t (*rng)())
    : base_ms_(base_ms), cap_ms_(cap_ms), jitter_pct_(jitter_pct > 100 ? 100 : jitter_pct), rng_(rng) {}

uint32_t Backoff::next() {
  // Shift guard: beyond 2^20 * base the value would overflow uint32 for any sane base,
  // and it is above every realistic cap anyway.
  uint64_t raw = (attempts_ >= 20) ? cap_ms_ : (uint64_t)base_ms_ << attempts_;
  uint32_t delay = raw > cap_ms_ ? cap_ms_ : (uint32_t)raw;
  if (attempts_ < UINT32_MAX) attempts_++;

  if (jitter_pct_ == 0 || rng_ == nullptr) return delay;
  // Spread the delay over [delay*(1-j), delay*(1+j)] so many nodes that lost the
  // broker at the same moment do not all reconnect in the same second.
  uint32_t span = 2u * jitter_pct_ + 1u;                   // number of percent steps
  int32_t pct = (int32_t)(rng_() % span) - (int32_t)jitter_pct_;  // -j .. +j
  int64_t jittered = (int64_t)delay + ((int64_t)delay * pct) / 100;
  if (jittered < 0) jittered = 0;
  return (uint32_t)jittered;
}

void Backoff::reset() { attempts_ = 0; }
