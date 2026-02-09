#include "hlv/phase_readiness.hpp"

#include <cmath>
#include <limits>

namespace hlv {

// Constructor
PhaseReadinessMiddleware::PhaseReadinessMiddleware(PhaseReadinessConfig cfg)
    : cfg_(cfg)
    , has_prev_(false)
    , prev_t_s_(0.0)
    , prev_temp_C_(std::numeric_limits<double>::quiet_NaN())
    , trend_dTdt_(0.0)
    , trend_age_s_(0.0)
{}

// Reset to initial fail-safe state
void PhaseReadinessMiddleware::reset() {
  has_prev_ = false;
  prev_t_s_ = 0.0;
  prev_temp_C_ = std::numeric_limits<double>::quiet_NaN();
  trend_dTdt_ = 0.0;
  trend_age_s_ = 0.0;
}

// Helper: check if value is finite
bool PhaseReadinessMiddleware::is_finite(double x) {
  return std::isfinite(x);
}

// Helper: clamp value to [0, 1]
double PhaseReadinessMiddleware::clamp01(double x) {
  return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
}

// Deterministic mapping: readiness → gate
static Gate gate_from_readiness(double r) {
  if (r >= 0.80) return Gate::ALLOW;
  if (r >= 0.40) return Gate::CAUTION;
  return Gate::BLOCK;
}

// Core evaluation: deterministic readiness inference
PhaseReadinessOutput PhaseReadinessMiddleware::evaluate(const PhaseSignals& in) {
  PhaseReadinessOutput out{};
  out.flags = FLAG_NONE;

  // Fail-safe helper
  auto fail_safe = [&](uint32_t reason_flag) -> PhaseReadinessOutput {
    out.readiness = 0.0;
    out.gate = Gate::BLOCK;
    out.flags |= reason_flag;
    out.flags |= FLAG_FAILSAFE_DEFAULT;
    out.dTdt_C_per_s = 0.0;
    out.trend_C = 0.0;
    out.stability_score = 0.0;
    return out;
  };

  // Step 1: Validate required inputs
  if (!in.valid || !is_finite(in.t_s) || !is_finite(in.temp_C)) {
    return fail_safe(FLAG_INPUT_INVALID);
  }

  // Step 2: Bootstrap - first sample
  if (!has_prev_) {
    has_prev_ = true;
    prev_t_s_ = in.t_s;
    prev_temp_C_ = in.temp_C;
    out.readiness = 0.0;
    out.gate = Gate::BLOCK;
    out.flags |= FLAG_STALE_OR_NONMONO;
    out.flags |= FLAG_FAILSAFE_DEFAULT;
    out.dTdt_C_per_s = 0.0;
    out.trend_C = 0.0;
    out.stability_score = 0.0;
    return out;
  }

  // Step 3: Temporal validation
  const double dt = in.t_s - prev_t_s_;
  
  if (dt <= 0.0) {
    return fail_safe(FLAG_STALE_OR_NONMONO);
  }
  
  if (dt > cfg_.max_dt_s) {
    return fail_safe(FLAG_STALE_OR_NONMONO);
  }

  const bool temp_out_of_range =
      (in.temp_C < cfg_.temp_min_C || in.temp_C > cfg_.temp_max_C);
  if (temp_out_of_range) {
    out.flags |= FLAG_TEMP_OUT_OF_RANGE;
  }

  // Step 3b: Sensor glitch guard (only for larger sample intervals)
  const double glitch_dt_threshold = cfg_.max_dt_s * 0.5;
  if (dt >= glitch_dt_threshold &&
      std::fabs(in.temp_C - prev_temp_C_) > cfg_.max_abs_temp_jump_C) {
    return fail_safe(FLAG_INPUT_INVALID);
  }

  // Step 4: Compute instantaneous derivative
  out.dTdt_C_per_s = (in.temp_C - prev_temp_C_) / dt;

  // Step 5: Update trend estimate (bounded EWMA)
  const double alpha = clamp01(cfg_.ewma_alpha);
  trend_dTdt_ = alpha * out.dTdt_C_per_s + (1.0 - alpha) * trend_dTdt_;

  const bool sign_consistent =
      (trend_dTdt_ >= 0.0 && out.dTdt_C_per_s >= 0.0) ||
      (trend_dTdt_ < 0.0 && out.dTdt_C_per_s < 0.0);

  if (sign_consistent) {
    trend_age_s_ += dt;
  } else {
    trend_age_s_ = 0.0;
  }

  out.trend_C = trend_dTdt_;

  // Step 6: Update previous state
  prev_t_s_ = in.t_s;
  prev_temp_C_ = in.temp_C;

  // Step 7: Apply eligibility constraints
  if (std::fabs(out.dTdt_C_per_s) > cfg_.max_abs_dTdt_C_per_s) {
    out.flags |= FLAG_GRADIENT_TOO_HIGH;
  }

  if (trend_age_s_ >= cfg_.persistence_s) {
    if (trend_dTdt_ > 0.0) {
      out.flags |= FLAG_PERSISTENT_HEATING;
    } else if (trend_dTdt_ < 0.0) {
      out.flags |= FLAG_PERSISTENT_COOLING;
    }
  }

  if (is_finite(in.hysteresis_index) &&
      in.hysteresis_index >= cfg_.hysteresis_block_threshold) {
    out.flags |= FLAG_HYSTERESIS_HIGH;
  }

  if (is_finite(in.coherence_index) &&
      in.coherence_index < cfg_.coherence_allow_threshold) {
    out.flags |= FLAG_COHERENCE_LOW;
  }

  // Step 8: Compute readiness score (deterministic penalties)
  double readiness = 1.0;

  if (out.flags & FLAG_TEMP_OUT_OF_RANGE)  readiness -= 0.60;
  if (out.flags & FLAG_GRADIENT_TOO_HIGH)  readiness -= 0.60;
  if (out.flags & FLAG_HYSTERESIS_HIGH)    readiness -= 0.70;
  if (out.flags & FLAG_COHERENCE_LOW)      readiness -= 0.30;
  if (out.flags & FLAG_PERSISTENT_HEATING) readiness -= 0.20;
  if (out.flags & FLAG_PERSISTENT_COOLING) readiness -= 0.10;

  out.readiness = clamp01(readiness);
  out.stability_score = out.readiness;

  // Step 9: Map to discrete gate
  out.gate = gate_from_readiness(out.readiness);

  // Step 10: Safety override
  const bool critical_violation =
      (out.flags & FLAG_TEMP_OUT_OF_RANGE) ||
      (out.flags & FLAG_GRADIENT_TOO_HIGH) ||
      (out.flags & FLAG_HYSTERESIS_HIGH);

  if (critical_violation) {
    out.readiness = 0.0;
    out.stability_score = 0.0;
    out.gate = Gate::BLOCK;
  }

  return out;
}

} // namespace hlv
