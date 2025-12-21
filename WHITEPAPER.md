# Deterministic Phase-Readiness Architecture for Closed-Loop Neurostimulation

## A Safety-First Middleware Approach for State-Aware Energy Delivery

**Marcel Krüger**  
Independent Researcher, Germany  
ORCID: 0009-0002-5709-9729

**Don Feeney**  
Independent Cognitive Systems Researcher, USA  
ORCID: 0009-0003-1350-4160

**December 21, 2025**

---

## Abstract

Closed-loop neurostimulation systems increasingly employ energetic or photonic interventions whose safety depends not only on stimulus parameters but on the instantaneous dynamical state of the biological system. This work introduces a deterministic Phase-Readiness Middleware (PRM) architecture designed to enforce state-aware eligibility constraints on energy delivery in closed-loop neurostimulation systems.

Rather than optimizing or adapting stimulation parameters, the middleware evaluates whether the monitored system resides in a geometrically stable and coherent state suitable for energy input. The architecture functions as a deterministic, inspectable gating layer between sensing and actuation, preventing energy delivery during identified instability regimes.

The proposed framework does not prescribe therapeutic actions and does not claim clinical efficacy. It provides a safety-oriented engineering layer compatible with regulatory requirements for determinism, traceability, and fail-safe behavior in medical-grade control systems.

**Keywords:** Closed-loop systems; neurostimulation safety; deterministic middleware; state-aware control; phase synchronization; medical device engineering

---

## 1. Introduction

Advances in neurostimulation and photonic neuromodulation increasingly rely on closed-loop architectures that couple real-time sensing with energetic intervention. While such systems offer improved responsiveness, they introduce new safety challenges related to the timing of energy delivery under dynamically evolving system states.

Existing approaches frequently focus on optimizing stimulation parameters or employ adaptive algorithms. In contrast, the present work addresses a complementary and foundational engineering question: **When is a system eligible to receive energy input at all?**

We propose a deterministic middleware architecture that operates independently of therapeutic intent and enforces state-dependent eligibility constraints on actuation.

---

## 2. Engineering Philosophy: Safety by Design

The proposed Phase-Readiness Middleware follows a safety-first engineering paradigm grounded in deterministic control theory.

The system is designed according to the following principles:

- **Determinism:** identical inputs yield identical gating decisions.
- **Inspectability:** all decision variables are explicit and logged.
- **Fail-safe behavior:** undefined or unstable states default to actuation block.
- **Non-interference:** existing stimulation protocols remain unmodified.

The middleware neither learns nor adapts. It evaluates externally supplied state indicators and enforces hard eligibility constraints on actuation timing.

---

## 3. Scope Separation: Physics vs Engineering

This work is strictly confined to system architecture and safety-oriented control. Underlying physical or biological models are treated as external providers of measured state variables.

The middleware does not validate, interpret, or modify these models. Its sole function is to handle externally supplied indicators within a deterministic, safety-critical control loop.

**No claim is made regarding the correctness of any specific physical or biological theory.**

```
┌─────────────────────┐
│  Physics/Biology    │
│      Layer          │
│                     │
│  (State variables,  │
│    coherence)       │
└──────────┬──────────┘
           │ Measured State
           │      ΔΦ
           ↓
┌─────────────────────┐
│ Phase-Readiness     │
│    Middleware       │
│                     │
│ (Deterministic Gate)│
└──────────┬──────────┘
           │ Enable/Block
           ↓
┌─────────────────────┐
│  Actuation Layer    │
│                     │
│ (Laser/Stimulator)  │
└─────────────────────┘
```

**Figure 1:** System Architecture: The conceptual separation between physical modeling, safety-critical middleware, and actuation.

---

## 4. Phase Readiness as an Engineering Variable

The middleware evaluates a scalar readiness score

**R ∈ [0, 1]**,

derived from externally supplied state indicators.

- **R = 1:** system eligible for energy delivery.
- **R = 0:** system unstable or undefined; actuation blocked.

The readiness score is not a therapeutic recommendation. It represents an eligibility constraint ensuring that energy delivery does not occur during identified instability regimes.

---

## 5. System Architecture

The Phase-Readiness Middleware is positioned between sensing modules (e.g. EEG, monitoring systems) and actuation hardware. It does not modify stimulation parameters but enforces temporal eligibility constraints on actuation.

| Component      | Function              | Safety Role        |
|----------------|----------------------|--------------------|
| Sensor Layer   | State measurement    | Observation only   |
| Middleware     | Readiness evaluation | Actuation gating   |
| Actuator       | Energy delivery      | Passive execution  |

**Table 1:** Functional separation of responsibilities. The color coding corresponds to the system architecture in Figure 1.

---

## 6. Deterministic Gate Logic

The middleware implements a binary gating logic:

```
         ⎧ Enabled,  R = 1,
Actuation = ⎨
         ⎩ Blocked,  R = 0.
```

Transitions between states are event-driven and logged. No continuous control or parameter optimization is performed.

```
                  Instability detected
               ┌─────────────────────────┐
               │                         │
               ↓                         │
    ┌──────────────────┐      ┌──────────────────┐
    │  Eligible State  │      │  Blocked State   │
    │     (R = 1)      │      │     (R = 0)      │
    └──────────────────┘      └──────────────────┘
               │                         ↑
               │                         │
               └─────────────────────────┘
                  Stability restored
```

**Figure 2:** Deterministic two-state gating logic. Energy delivery is only permitted in the eligible state (R = 1).

---

## 7. Regulatory Considerations

The proposed architecture aligns with regulatory expectations for:

- deterministic control behavior,
- traceable decision logic,
- fail-safe default states,
- separation of monitoring and actuation.

The middleware is compatible with medical device development frameworks such as **IEC 62304** and risk management processes defined in **ISO 14971**.

---

## 8. Limitations

This work does **not**:

- claim clinical efficacy,
- replace medical judgment,
- define treatment protocols,
- introduce autonomous decision-making.

It provides a deterministic engineering framework for safe operation under dynamic state uncertainty.

---

## Code and Reproducibility

Reference implementations and illustrative simulations related to the architecture are publicly available at:

**https://github.com/tlacaelel666/hlv-demo**
