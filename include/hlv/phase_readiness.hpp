#pragma once

// Implementation of: "Deterministic Phase-Readiness Architecture for
// Closed-Loop Neurostimulation" (Krüger & Feeney, 2025)
//
// SAFETY PRINCIPLES (paper Section 2):
// 1. Determinism: identical inputs → identical outputs
// 2. Inspectability: all decisions are explicit and logged
// 3. Fail-safe: undefined/unstable → BLOCK
// 4. Non-interference: does not modify stimulation protocols
//
// IMPORTANT NON-GOALS (paper Section 8):
// - Does NOT claim clinical efficacy
// - Does NOT replace medical judgment
// - Does NOT define treatment protocols
// - Does NOT introduce autonomous decision-making

#include <cstdint>
#include <cmath>
#include <limits>

namespace hlv {

// Gate implements deterministic state logic (paper Figure 2, Section 6):
// - BLOCK (R≈0): unstable/undefined, actuation blocked
// - ALLOW (R≈1): eligible for energy delivery
// - CAUTION: intermediate state for gradual transitions (implementation extension)
enum class Gate : uint8_t { 
  BLOCK = 0,
  CAUTION = 1,
  ALLOW = 2
};

// Fully inspectable, loggable input snapshot.
// Physics/biology layer (paper Section 3): External models provide these values.
// This middleware does NOT validate, interpret, or modify physical models.
struct PhaseSignals final {
  double t_s = 0.0;
  double temp_C = std::numeric_limits<double>::quiet_NaN();
  double temp_ambient_C = std::numeric_limits<double>::quiet_NaN();
  double hysteresis_index = std::numeric_limits<double>::quiet_NaN();
  double coherence_index = std::numeric_limits<double>::quiet_NaN();
  bool valid = false;
};

// Deterministic, inspectable outputs.
// R ∈ [0,1] (paper Section 4): R=1 eligible, R=0 blocked
struct PhaseReadinessOutput final {
  double readiness = 0.0;
  Gate gate = Gate::BLOCK;
  uint32_t flags = 0;
  double dTdt_C_per_s = 0.0;
  double trend_C = 0.0;
  double stability_score = 0.0;
};

// Bit flags for explainability (paper Section 2: inspectability)
enum PhaseFlags : uint32_t {
  FLAG_NONE                 = 0,
  FLAG_INPUT_INVALID        = 1u << 0,
  FLAG_STALE_OR_NONMONO     = 1u << 1,
  FLAG_TEMP_OUT_OF_RANGE    = 1u << 2,
  FLAG_GRADIENT_TOO_HIGH    = 1u << 3,
  FLAG_PERSISTENT_HEATING   = 1u << 4,
  FLAG_PERSISTENT_COOLING   = 1u << 5,
  FLAG_HYSTERESIS_HIGH      = 1u << 6,
  FLAG_COHERENCE_LOW        = 1u << 7,
  FLAG_FAILSAFE_DEFAULT     = 1u << 31
};

// Configuration: explicit policy parameters (IEC 62304 compliance)
struct PhaseReadinessConfig final {
  double temp_min_C = -20.0;
  double temp_max_C =  60.0;
  double max_abs_dTdt_C_per_s = 0.25;
  double max_abs_temp_jump_C = 5.0;
  double ewma_alpha = 0.2;
  double persistence_s = 3.0;
  double hysteresis_block_threshold = 0.85;
  double coherence_allow_threshold  = 0.35;
  double max_dt_s = 1.0;
};

// Phase Readiness Middleware (paper Section 5, Figure 1)
// Safety-critical, deterministic eligibility gate.
// Stateless w.r.t. actuation; stateful only for short-term history.
class PhaseReadinessMiddleware final {
public:
  explicit PhaseReadinessMiddleware(PhaseReadinessConfig cfg);
  
  void reset();
  PhaseReadinessOutput evaluate(const PhaseSignals& in);

private:
  PhaseReadinessConfig cfg_;
  bool   has_prev_;
  double prev_t_s_;
  double prev_temp_C_;
  double trend_dTdt_;
  double trend_age_s_;
  
  static bool is_finite(double x);
  static double clamp01(double x);
};

} // namespace hlv
