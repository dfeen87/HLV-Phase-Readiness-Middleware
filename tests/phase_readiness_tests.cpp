#include "hlv/phase_readiness.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace hlv;

// NOTE: Readiness thresholds and penalties are policy parameters,
// not clinical thresholds. Tests assert relative behavior only.
// Gate threshold mappings (gate_from_readiness) are implementation
// details and may be tuned per deployment without invalidating safety.

// -----------------------------------------------------------------------------
// Helper: build a valid baseline signal
// -----------------------------------------------------------------------------
static PhaseSignals make_valid_signal(double t, double temp) {
  PhaseSignals s;
  s.t_s = t;
  s.temp_C = temp;
  s.valid = true;
  // Leave other fields as NaN (optional indicators)
  return s;
}

// -----------------------------------------------------------------------------
// Test 1: Initial sample must fail-safe (no derivative context)
// -----------------------------------------------------------------------------
static void test_bootstrap_failsafe() {
  PhaseReadinessMiddleware mw(PhaseReadinessConfig{});

  auto out = mw.evaluate(make_valid_signal(0.0, 25.0));

  assert(out.gate == Gate::BLOCK);
  assert(out.flags & FLAG_STALE_OR_NONMONO);
  assert(out.flags & FLAG_FAILSAFE_DEFAULT);
  assert(out.readiness == 0.0);
}

// -----------------------------------------------------------------------------
// Test 2: Stable conditions → ALLOW
// -----------------------------------------------------------------------------
static void test_stable_allows() {
  PhaseReadinessMiddleware mw(PhaseReadinessConfig{});

  // Prime middleware
  mw.evaluate(make_valid_signal(0.0, 25.0));

  auto out = mw.evaluate(make_valid_signal(0.5, 25.05));

  // Test readiness first (future-proof against gate threshold changes)
  assert(out.readiness >= 0.8);
  assert(out.gate == Gate::ALLOW);
  assert(out.flags == FLAG_NONE);
  assert(std::isfinite(out.dTdt_C_per_s));
  assert(std::isfinite(out.trend_C));
}

// -----------------------------------------------------------------------------
// Test 3: Temperature out of range → BLOCK
// -----------------------------------------------------------------------------
static void test_temp_out_of_range_blocks() {
  PhaseReadinessMiddleware mw(PhaseReadinessConfig{});

  mw.evaluate(make_valid_signal(0.0, 25.0));
  auto out = mw.evaluate(make_valid_signal(0.5, 120.0));

  // Test fundamental constraint violation
  assert(out.flags & FLAG_TEMP_OUT_OF_RANGE);
  assert(out.readiness < 0.5); // Should be heavily penalized
  assert(out.gate == Gate::BLOCK); // Critical violation forces BLOCK
}

// -----------------------------------------------------------------------------
// Test 4: Excessive gradient → BLOCK
// -----------------------------------------------------------------------------
static void test_gradient_blocks() {
  PhaseReadinessMiddleware mw(PhaseReadinessConfig{});

  mw.evaluate(make_valid_signal(0.0, 20.0));
  auto out = mw.evaluate(make_valid_signal(0.1, 40.0)); // 200 °C/s rate!

  assert(out.gate == Gate::BLOCK);
  assert(out.flags & FLAG_GRADIENT_TOO_HIGH);
  assert(out.readiness < 0.5);
}

// -----------------------------------------------------------------------------
// Test 5: Sensor glitch detection
// -----------------------------------------------------------------------------
static void test_sensor_glitch_blocks() {
  PhaseReadinessMiddleware mw(PhaseReadinessConfig{});

  mw.evaluate(make_valid_signal(0.0, 25.0));
  auto out = mw.evaluate(make_valid_signal(0.5, 100.0)); // 75°C jump (implausible)

  assert(out.gate == Gate::BLOCK);
  assert(out.flags & FLAG_INPUT_INVALID);
  assert(out.flags & FLAG_FAILSAFE_DEFAULT);
}

// -----------------------------------------------------------------------------
// Test 6: Coherence drop → CAUTION or BLOCK
// -----------------------------------------------------------------------------
static void test_low_coherence_penalty() {
  PhaseReadinessMiddleware mw(PhaseReadinessConfig{});

  mw.evaluate(make_valid_signal(0.0, 25.0));

  PhaseSignals s = make_valid_signal(0.5, 25.0);
  s.coherence_index = 0.1; // Very low coherence

  auto out = mw.evaluate(s);

  // Test constraint and relative behavior (exact gate depends on thresholds)
  assert(out.flags & FLAG_COHERENCE_LOW);
  assert(out.readiness < 0.8); // Should be penalized
  assert(out.gate != Gate::ALLOW); // Should not be fully allowed
}

// -----------------------------------------------------------------------------
// Test 7: High hysteresis → BLOCK
// -----------------------------------------------------------------------------
static void test_high_hysteresis_blocks() {
  PhaseReadinessMiddleware mw(PhaseReadinessConfig{});

  mw.evaluate(make_valid_signal(0.0, 25.0));

  PhaseSignals s = make_valid_signal(0.5, 25.0);
  s.hysteresis_index = 0.9; // High hysteresis

  auto out = mw.evaluate(s);

  assert(out.gate == Gate::BLOCK);
  assert(out.flags & FLAG_HYSTERESIS_HIGH);
}

// -----------------------------------------------------------------------------
// Test 8: Determinism check (same input → same output)
// -----------------------------------------------------------------------------
static void test_determinism() {
  PhaseReadinessMiddleware mw1(PhaseReadinessConfig{});
  PhaseReadinessMiddleware mw2(PhaseReadinessConfig{});

  mw1.evaluate(make_valid_signal(0.0, 25.0));
  mw2.evaluate(make_valid_signal(0.0, 25.0));

  auto out1 = mw1.evaluate(make_valid_signal(0.5, 25.0));
  auto out2 = mw2.evaluate(make_valid_signal(0.5, 25.0));

  assert(out1.gate == out2.gate);
  assert(out1.flags == out2.flags);
  assert(std::fabs(out1.readiness - out2.readiness) < 1e-9);
  assert(std::fabs(out1.dTdt_C_per_s - out2.dTdt_C_per_s) < 1e-9);
}

// -----------------------------------------------------------------------------
// Test 9: Non-monotonic time → fail-safe
// -----------------------------------------------------------------------------
static void test_nonmonotonic_time_blocks() {
  PhaseReadinessMiddleware mw(PhaseReadinessConfig{});

  mw.evaluate(make_valid_signal(1.0, 25.0));
  auto out = mw.evaluate(make_valid_signal(0.5, 25.0)); // Time went backwards!

  assert(out.gate == Gate::BLOCK);
  assert(out.flags & FLAG_STALE_OR_NONMONO);
  assert(out.flags & FLAG_FAILSAFE_DEFAULT);
}

// -----------------------------------------------------------------------------
// Test 10: Stale data → fail-safe
// -----------------------------------------------------------------------------
static void test_stale_data_blocks() {
  PhaseReadinessConfig cfg;
  cfg.max_dt_s = 1.0;
  PhaseReadinessMiddleware mw(cfg);

  mw.evaluate(make_valid_signal(0.0, 25.0));
  auto out = mw.evaluate(make_valid_signal(5.0, 25.0)); // 5s gap (too large)

  assert(out.gate == Gate::BLOCK);
  assert(out.flags & FLAG_STALE_OR_NONMONO);
}

// -----------------------------------------------------------------------------
// Test 11: Invalid input → fail-safe
// -----------------------------------------------------------------------------
static void test_invalid_input_blocks() {
  PhaseReadinessMiddleware mw(PhaseReadinessConfig{});

  PhaseSignals s;
  s.t_s = 0.0;
  s.temp_C = 25.0;
  s.valid = false; // Invalid data

  auto out = mw.evaluate(s);

  assert(out.gate == Gate::BLOCK);
  assert(out.flags & FLAG_INPUT_INVALID);
}

// -----------------------------------------------------------------------------
// Test 12: Reset functionality
// -----------------------------------------------------------------------------
static void test_reset() {
  PhaseReadinessMiddleware mw(PhaseReadinessConfig{});

  // Prime middleware
  mw.evaluate(make_valid_signal(0.0, 25.0));
  mw.evaluate(make_valid_signal(0.5, 25.0));

  // Reset should clear history
  mw.reset();

  // First sample after reset should fail-safe again
  auto out = mw.evaluate(make_valid_signal(1.0, 25.0));

  assert(out.gate == Gate::BLOCK);
  assert(out.flags & FLAG_STALE_OR_NONMONO);
}

// -----------------------------------------------------------------------------
// Test 13: Persistent heating detection
// -----------------------------------------------------------------------------
static void test_persistent_heating() {
  PhaseReadinessConfig cfg;
  cfg.persistence_s = 1.0;
  PhaseReadinessMiddleware mw(cfg);

  // Establish baseline
  mw.evaluate(make_valid_signal(0.0, 20.0));

  // Gradual heating over time
  mw.evaluate(make_valid_signal(0.3, 20.05));
  mw.evaluate(make_valid_signal(0.6, 20.10));
  mw.evaluate(make_valid_signal(0.9, 20.15));
  auto out = mw.evaluate(make_valid_signal(1.5, 20.20));

  assert(out.flags & FLAG_PERSISTENT_HEATING);
}

// -----------------------------------------------------------------------------
// Test 14: NaN handling (optional indicators)
// -----------------------------------------------------------------------------
static void test_nan_optional_indicators() {
  PhaseReadinessMiddleware mw(PhaseReadinessConfig{});

  mw.evaluate(make_valid_signal(0.0, 25.0));

  PhaseSignals s = make_valid_signal(0.5, 25.0);
  // Leave coherence_index and hysteresis_index as NaN (default)
  s.coherence_index = std::numeric_limits<double>::quiet_NaN();
  s.hysteresis_index = std::numeric_limits<double>::quiet_NaN();

  auto out = mw.evaluate(s);

  // Should not flag coherence or hysteresis issues when NaN
  assert(!(out.flags & FLAG_COHERENCE_LOW));
  assert(!(out.flags & FLAG_HYSTERESIS_HIGH));
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main() {
  std::cout << "Running Phase Readiness Middleware tests...\n";

  test_bootstrap_failsafe();
  test_stable_allows();
  test_temp_out_of_range_blocks();
  test_gradient_blocks();
  test_sensor_glitch_blocks();
  test_low_coherence_penalty();
  test_high_hysteresis_blocks();
  test_determinism();
  test_nonmonotonic_time_blocks();
  test_stale_data_blocks();
  test_invalid_input_blocks();
  test_reset();
  test_persistent_heating();
  test_nan_optional_indicators();

  std::cout << "[PASS] All Phase Readiness Middleware tests passed!\n";
  return 0;
}
