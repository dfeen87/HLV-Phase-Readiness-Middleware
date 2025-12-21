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
//
// Discrete gate output (what control layers consume)
enum class Gate : uint8_t { 
  BLOCK = 0,    // Energy delivery prohibited
  CAUTION = 1,  // Transitional/marginal state
  ALLOW = 2     // Energy delivery permitted
};

// Fully inspectable, loggable input snapshot.
// Physics/biology layer (paper Section 3): External models provide these values.
// This middleware does NOT validate, interpret, or modify physical models.
//
// You can wire this from any telemetry source (CAN, ADC, sensors, etc.).
struct PhaseSignals final {
  double t_s = 0.0;  // monotonic timestamp (seconds)
                     // Non-monotonic updates trigger FLAG_STALE_OR_NONMONO
  
  double temp_C = std::numeric_limits<double>::quiet_NaN();         // absolute temperature (or thermal proxy)
  double temp_ambient_C = std::numeric_limits<double>::quiet_NaN(); // optional ambient reference
  
  // Optional externally supplied indicators (can be NaN if not available)
  double hysteresis_index = std::numeric_limits<double>::quiet_NaN(); // e.g., 0..1 (higher = more hysteresis)
  
  double coherence_index = std::numeric_limits<double>::quiet_NaN();  // e.g., 0..1 (phase coherence, paper Section 3)
                                                                       // Higher = more stable/coherent
                                                                       // Can represent ΔΦ or other stability metrics
  
  // Data quality
  bool valid = false;  // telemetry validity from upstream
};

// Deterministic, inspectable outputs.
// "readiness" is a normalized eligibility score (R ∈ [0,1], paper Section 4);
// "gate" is a discrete policy-friendly state.
struct PhaseReadinessOutput final {
  double readiness = 0.0; // R ∈ [0,1] (paper Section 4)
                          // R=1: system eligible for energy delivery
                          // R=0: system unstable/undefined; actuation blocked
  
  Gate gate = Gate::BLOCK;  // Discrete actuation gate
  
  // Inspectability: why did we decide this?
  uint32_t flags = 0;           // bitmask of reasons (see PhaseFlags)
  double dTdt_C_per_s = 0.0;    // instantaneous temperature derivative
  double trend_C = 0.0;         // smoothed derivative estimate (bounded)
  double stability_score = 0.0; // intermediate [0..1] stability metric
};

// Bit flags for explainability (OR together)
// These provide traceable, loggable decision variables (paper Section 2)
enum PhaseFlags : uint32_t {
  FLAG_NONE                 = 0,
  FLAG_INPUT_INVALID        = 1u << 0,  // Input data quality failure
  FLAG_STALE_OR_NONMONO     = 1u << 1,  // Timestamp issue
  FLAG_TEMP_OUT_OF_RANGE    = 1u << 2,  // Outside operating band
  FLAG_GRADIENT_TOO_HIGH    = 1u << 3,  // |dT/dt| exceeds limit
  FLAG_PERSISTENT_HEATING   = 1u << 4,  // Sustained positive trend
  FLAG_PERSISTENT_COOLING   = 1u << 5,  // Sustained negative trend
  FLAG_HYSTERESIS_HIGH      = 1u << 6,  // Hysteresis index too high
  FLAG_COHERENCE_LOW        = 1u << 7,  // Coherence index too low
  FLAG_FAILSAFE_DEFAULT     = 1u << 31  // Fail-safe fallback triggered
};

// Configuration is explicit and auditable (regulatory compliance: IEC 62304).
// All parameters are policy decisions, not learned behaviors.
// No hidden domain heuristics: these are *policy parameters* you set per deployment.
struct PhaseReadinessConfig final {
  // Valid operating temperature band (eligibility constraint)
  double temp_min_C = -20.0;
  double temp_max_C =  60.0;
  
  // Derivative limits (eligibility constraint)
  double max_abs_dTdt_C_per_s = 0.25; // e.g. 0.25 °C/s
  
  // Sensor glitch detection (defensive validation)
  double max_abs_temp_jump_C = 5.0;   // maximum plausible temperature jump between samples
  
  // Persistence logic (short memory, deterministic)
  double ewma_alpha = 0.2;            // bounded smoothing for trend
  double persistence_s = 3.0;         // how long "trend" must persist to matter
  
  // Optional indicators (if provided)
  double hysteresis_block_threshold = 0.85; // if hysteresis_index >= this => block
  double coherence_allow_threshold  = 0.35; // if coherence_index < this => caution/block
  
  // Staleness (fail-safe timing)
  double max_dt_s = 1.0;  // if sample gap too large => stale => fail-safe
};

// Phase Readiness Middleware (paper Section 5, Figure 1)
// Architectural Position: Between sensing and actuation layers
// Role: Deterministic gating, NOT parameter optimization or control
//
// Safety-critical, deterministic eligibility gate.
// Stateless w.r.t. actuation; stateful only for short-term history.
//
// System Architecture (paper Figure 1):
//   [ Sensors / Telemetry ]
//            ↓
//   [ Phase Readiness Middleware ]  ← (this class)
//            ↓
//   [ Control / Policy / Optimization ]
//            ↓
//   [ Actuation ]
//
// Deterministic phase readiness middleware.
// It does NOT control anything: it only emits readiness + gate + reasons.
//
// IMPORTANT NON-GOALS (paper Section 8):
// - Does NOT claim clinical efficacy
// - Does NOT replace medical judgment
// - Does NOT define treatment protocols
// - Does NOT introduce autonomous decision-making
class PhaseReadinessMiddleware final {
public:
  explicit PhaseReadinessMiddleware(PhaseReadinessConfig cfg) : cfg_(cfg) {}
  
  // Reset internal memory (e.g., startup, sensor fault recovery).
  // Returns middleware to initial fail-safe state.
  void reset();
  
  // Evaluate readiness for a single input snapshot.
  // This is the core deterministic evaluation function (paper Section 6).
  // Identical inputs always yield identical outputs.
  PhaseReadinessOutput evaluate(const PhaseSignals& in);

private:
  PhaseReadinessConfig cfg_;
  
  // Internal state (small, deterministic memory)
  bool   has_prev_ = false;
  double prev_t_s_ = 0.0;
  double prev_temp_C_ = NAN;
  
  // EWMA trend of dT/dt (bounded smoothing, no learning)
  double trend_dTdt_ = 0.0;
  double trend_age_s_ = 0.0; // how long current trend has persisted
  
  // Helpers
  static bool is_finite(double x) { return std::isfinite(x); }
  static double clamp01(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }
};

} // namespace hlv
