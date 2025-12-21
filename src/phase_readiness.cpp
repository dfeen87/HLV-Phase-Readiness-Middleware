// Implementation of: "Deterministic Phase-Readiness Architecture for
// Closed-Loop Neurostimulation" (Krüger & Feeney, 2025)
//
// This implementation file provides the deterministic evaluation logic
// for the Phase-Readiness Middleware (paper Section 6).

#include "hlv/phase_readiness.hpp" // adjust include path to match your repo layout

#include <algorithm> // std::fabs
#include <cmath>     // std::isfinite

namespace hlv {

// ============================================================================
// Reset: Return to initial fail-safe state
// ============================================================================
void PhaseReadinessMiddleware::reset() {
  has_prev_ = false;
  prev_t_s_ = 0.0;
  prev_temp_C_ = NAN;
  trend_dTdt_ = 0.0;
  trend_age_s_ = 0.0;
}

// ============================================================================
// Deterministic mapping: readiness scalar → discrete gate
// ============================================================================
// This mapping implements the gate logic from paper Section 6 / Figure 2.
// CAUTION is an implementation extension for gradual transitions.
//
// Thresholds are explicit policy decisions and can be adjusted per deployment.
static Gate gate_from_readiness(double r) {
  // High readiness: system eligible for energy delivery
  if (r >= 0.80) return Gate::ALLOW;
  
  // Medium readiness: transitional/marginal state
  if (r >= 0.40) return Gate::CAUTION;
  
  // Low readiness: system unstable/undefined, actuation blocked
  return Gate::BLOCK;
}

// ============================================================================
// Core Evaluation: Deterministic readiness inference
// ============================================================================
PhaseReadinessOutput PhaseReadinessMiddleware::evaluate(const PhaseSignals& in) {
  PhaseReadinessOutput out{};
  out.flags = FLAG_NONE;

  // ------------------------------------------------------------------------
  // Fail-safe helper: BLOCK + explicit trace flags (paper Section 2)
  // ------------------------------------------------------------------------
  // Ensures all failure modes are inspectable and logged.
  // Undefined or unstable states default to actuation block.
  auto fail_safe = [&](uint32_t reason_flag) -> PhaseReadinessOutput {
    out.readiness = 0.0;                    // R = 0 (paper Section 4)
    out.gate = Gate::BLOCK;                 // Actuation blocked (paper Section 6)
    out.flags |= reason_flag;               // Specific reason
    out.flags |= FLAG_FAILSAFE_DEFAULT;     // General fail-safe indicator
    out.dTdt_C_per_s = 0.0;
    out.trend_C = 0.0;
    out.stability_score = 0.0;
    return out;
  };

  // ------------------------------------------------------------------------
  // Step 1: Validate required inputs (deterministic, inspectable)
  // ------------------------------------------------------------------------
  // Invalid inputs cannot be safely processed → fail-safe default.
  if (!in.valid || !is_finite(in.t_s) || !is_finite(in.temp_C)) {
    return fail_safe(FLAG_INPUT_INVALID);
  }

  // ------------------------------------------------------------------------
  // Step 2: Bootstrap - first sample cannot compute derivative
  // ------------------------------------------------------------------------
  // This is not an error condition, but insufficient temporal context.
  // We store the first sample and return fail-safe until we have a derivative.
  if (!has_prev_) {
    has_prev_ = true;
    prev_t_s_ = in.t_s;
    prev_temp_C_ = in.temp_C;
    
    // First sample: no derivative context yet → fail-safe
    return fail_safe(FLAG_STALE_OR_NONMONO);
  }

  // ------------------------------------------------------------------------
  // Step 3: Temporal validation (monotonic & staleness)
  // ------------------------------------------------------------------------
  // Time must advance monotonically and within acceptable intervals.
  // Non-monotonic time or excessive gaps indicate telemetry issues.
  const double dt = in.t_s - prev_t_s_;
  
  if (dt <= 0.0) {
    // Non-monotonic time: refuse inference
    // Do NOT update prev_ state to avoid corrupting future evaluations
    return fail_safe(FLAG_STALE_OR_NONMONO);
  }
  
  if (dt > cfg_.max_dt_s) {
    // Sample gap too large: data may be stale
    // Update prev_ state to avoid accumulating stale intervals
    has_prev_ = true;
    prev_t_s_ = in.t_s;
    prev_temp_C_ = in.temp_C;
    return fail_safe(FLAG_STALE_OR_NONMONO);
  }

  // ------------------------------------------------------------------------
  // Step 3b: Sensor glitch guard (defensive validation)
  // ------------------------------------------------------------------------
  // Catch ADC spikes or sensor malfunctions that produce physically
  // implausible temperature jumps. This is a practical safety check
  // that doesn't alter the model but prevents garbage-in scenarios.
  constexpr double max_abs_temp_jump_C = 5.0;
  
  if (std::fabs(in.temp_C - prev_temp_C_) > max_abs_temp_jump_C) {
    // Physically implausible temperature jump detected
    // This likely indicates sensor glitch, ADC spike, or telemetry corruption
    return fail_safe(FLAG_INPUT_INVALID);
  }

  // ------------------------------------------------------------------------
  // Step 4: Compute instantaneous temperature derivative
  // ------------------------------------------------------------------------
  // This is the primary thermal gradient signal.
  out.dTdt_C_per_s = (in.temp_C - prev_temp_C_) / dt;

  // ------------------------------------------------------------------------
  // Step 5: Update deterministic trend estimate (bounded EWMA)
  // ------------------------------------------------------------------------
  // EWMA provides bounded, deterministic smoothing without learning.
  // This is NOT adaptive AI - it's a fixed-parameter low-pass filter.
  //
  // Defensive clamping: prevent pathological configurations from degrading
  // behavior quietly. If misconfigured, we clamp rather than fail.
  const double alpha = clamp01(cfg_.ewma_alpha);
  trend_dTdt_ = alpha * out.dTdt_C_per_s + (1.0 - alpha) * trend_dTdt_;

  // Track persistence: how long has the trend direction remained consistent?
  // This captures the "recent history" aspect mentioned in paper Section 4.
  const bool sign_consistent =
      (trend_dTdt_ >= 0.0 && out.dTdt_C_per_s >= 0.0) ||
      (trend_dTdt_ < 0.0 && out.dTdt_C_per_s < 0.0);

  if (sign_consistent) {
    trend_age_s_ += dt;
  } else {
    // Trend direction changed: reset persistence counter
    trend_age_s_ = 0.0;
  }

  out.trend_C = trend_dTdt_;

  // ------------------------------------------------------------------------
  // Step 6: Update previous state (only after passing temporal validation)
  // ------------------------------------------------------------------------
  prev_t_s_ = in.t_s;
  prev_temp_C_ = in.temp_C;

  // ------------------------------------------------------------------------
  // Step 7: Apply eligibility constraints (paper Section 4)
  // ------------------------------------------------------------------------
  // These constraints define when energy input is unsafe or unwise.
  // All violations are logged in flags for full inspectability.

  // Absolute temperature bounds
  if (in.temp_C < cfg_.temp_min_C || in.temp_C > cfg_.temp_max_C) {
    out.flags |= FLAG_TEMP_OUT_OF_RANGE;
  }

  // Temperature rate of change bounds
  if (std::fabs(out.dTdt_C_per_s) > cfg_.max_abs_dTdt_C_per_s) {
    out.flags |= FLAG_GRADIENT_TOO_HIGH;
  }

  // Persistent trends (thermal history, paper Section 4)
  if (trend_age_s_ >= cfg_.persistence_s) {
    if (trend_dTdt_ > 0.0) {
      out.flags |= FLAG_PERSISTENT_HEATING;
    } else if (trend_dTdt_ < 0.0) {
      out.flags |= FLAG_PERSISTENT_COOLING;
    }
  }

  // Optional external indicators (hysteresis, coherence)
  // These come from external physics/biology models (paper Section 3)
  if (is_finite(in.hysteresis_index) &&
      in.hysteresis_index >= cfg_.hysteresis_block_threshold) {
    out.flags |= FLAG_HYSTERESIS_HIGH;
  }

  if (is_finite(in.coherence_index) &&
      in.coherence_index < cfg_.coherence_allow_threshold) {
    out.flags |= FLAG_COHERENCE_LOW;
  }

  // ------------------------------------------------------------------------
  // Step 8: Compute readiness score from flags (deterministic penalties)
  // ------------------------------------------------------------------------
  // Start at maximum readiness (R=1.0) and subtract penalties for violations.
  // This is a simple, auditable, deterministic aggregation.
  //
  // IMPORTANT: Penalties are not probabilities or predictions.
  // They encode conservative eligibility heuristics only.
  //
  // Penalty weights are explicit policy decisions and should be tuned
  // per deployment based on domain requirements and risk tolerance.
  double readiness = 1.0;

  // Critical violations: large penalties
  if (out.flags & FLAG_TEMP_OUT_OF_RANGE)  readiness -= 0.60;
  if (out.flags & FLAG_GRADIENT_TOO_HIGH)  readiness -= 0.50;
  if (out.flags & FLAG_HYSTERESIS_HIGH)    readiness -= 0.70;

  // Moderate violations: medium penalties
  if (out.flags & FLAG_COHERENCE_LOW)      readiness -= 0.30;
  if (out.flags & FLAG_PERSISTENT_HEATING) readiness -= 0.20;

  // Minor violations: small penalties
  if (out.flags & FLAG_PERSISTENT_COOLING) readiness -= 0.10;

  // Clamp to valid range [0, 1]
  out.readiness = clamp01(readiness);
  
  // Stability score: for now, same as readiness
  // This provides a hook for future separation if needed
  out.stability_score = out.readiness;

  // ------------------------------------------------------------------------
  // Step 9: Map readiness scalar to discrete gate
  // ------------------------------------------------------------------------
  out.gate = gate_from_readiness(out.readiness);

  // ------------------------------------------------------------------------
  // Step 10: Safety override - critical violations force BLOCK
  // ------------------------------------------------------------------------
  // Certain violations are severe enough to override the gate mapping.
  // This implements the hard fail-safe requirement (paper Section 2).
  const bool critical_violation =
      (out.flags & FLAG_TEMP_OUT_OF_RANGE) ||
      (out.flags & FLAG_GRADIENT_TOO_HIGH) ||
      (out.flags & FLAG_HYSTERESIS_HIGH);

  if (critical_violation) {
    out.gate = Gate::BLOCK;
  }

  // ------------------------------------------------------------------------
  // Return: All outputs are deterministic and inspectable
  // ------------------------------------------------------------------------
  return out;
}

} // namespace hlv
