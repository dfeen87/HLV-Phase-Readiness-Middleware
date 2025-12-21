# Phase Readiness Middleware Architecture

## Overview

The Phase Readiness Middleware implements a deterministic, inspectable eligibility gate positioned between sensing and actuation layers in closed-loop systems.

Its sole responsibility is to decide whether energy delivery is currently permissible, based on system state, temporal consistency, and recent history.

**It does not:**
- control actuators
- optimize parameters
- infer clinical intent
- modify upstream or downstream logic

This separation is intentional and foundational to the architecture.

## Architectural Position

```
[ Sensors / Telemetry ]
          ↓
[ Phase Readiness Middleware ]   ← this component
          ↓
[ Control / Policy / Optimization ]
          ↓
[ Actuation ]
```

The middleware observes state and emits eligibility, but never issues commands.

## Core Design Principles

### 1. Determinism

Identical input sequences always produce identical outputs.

- No randomness
- No learning
- No adaptive state beyond bounded short-term history
- No dependency on wall-clock time

This enables reproducibility, auditing, and formal reasoning.

### 2. Inspectability

Every decision is fully explainable.

Each evaluation returns:
- a normalized readiness score **R ∈ [0,1]**
- a discrete gate (`BLOCK`, `CAUTION`, `ALLOW`)
- a bitmask of explicit reasons (`PhaseFlags`)
- intermediate diagnostics (derivatives, trends)

No implicit logic paths exist.

### 3. Fail-Safe Default

Undefined, unstable, or ambiguous states always block.

Fail-safe conditions include (non-exhaustive):
- invalid input
- non-monotonic time
- stale data
- implausible sensor jumps
- first-sample bootstrap
- critical constraint violations

Fail-safe behavior is explicit and logged.

### 4. Non-Interference

The middleware does not:
- alter stimulation protocols
- adjust controller gains
- interpret biological meaning
- introduce autonomous decisions

It only gates eligibility.

## Data Flow

### Inputs (`PhaseSignals`)

Inputs are snapshots, not streams.

They may include:
- monotonic timestamp
- primary state variable (e.g., temperature)
- optional externally-computed indicators (e.g., coherence, hysteresis)
- a validity flag from upstream systems

The middleware does not validate physical models — it assumes upstream responsibility.

### Outputs (`PhaseReadinessOutput`)

Each evaluation produces:
- **readiness:** normalized eligibility score
- **gate:** discrete policy-friendly state
- **flags:** explainable reasons for penalties or blocking
- **diagnostics:** for logging and audit

Outputs are deterministic and side-effect free.

## Internal State Model

The middleware maintains minimal bounded state:
- previous timestamp
- previous primary signal value
- smoothed derivative estimate (EWMA)
- persistence duration of current trend

This state:
- exists only to evaluate recent history
- resets explicitly via `reset()`
- never leaks across instances

There is no long-term memory.

## Readiness Computation

Readiness is computed conservatively:

1. Start from **R = 1.0**
2. Apply explicit penalties for constraint violations
3. Clamp to **[0, 1]**
4. Map readiness to a discrete gate
5. Apply hard safety overrides for critical violations

Penalty weights are policy parameters, not clinical claims.

## Gate Semantics

| Gate      | Meaning                             |
|-----------|-------------------------------------|
| `BLOCK`   | Energy delivery prohibited          |
| `CAUTION` | Transitional / marginal eligibility |
| `ALLOW`   | Energy delivery permitted           |

Gate mapping is deterministic and auditable.

## Configuration Model

All thresholds and limits are supplied via `PhaseReadinessConfig`.

They are:
- explicit
- static
- deployment-specific
- non-learned

This supports regulatory traceability (e.g., IEC 62304 style separation).

## Safety Boundary

This middleware explicitly does **not** claim:
- therapeutic efficacy
- clinical safety
- optimal dosing
- medical judgment replacement

It enforces structural safety constraints only.

All domain meaning remains outside the component.

## Intended Domains

While originally motivated by closed-loop neurostimulation, the architecture is domain-agnostic and applicable to:

- biomedical systems
- battery management systems
- thermal protection systems
- propulsion gating
- power electronics
- industrial safety interlocks

The middleware does not embed domain assumptions.

## Verification Strategy

Verification focuses on behavioral invariants, not outcomes:

- determinism
- fail-safe correctness
- monotonic time enforcement
- bounded state behavior
- explicit reason reporting

All tests assert relative behavior, not fixed thresholds.

## Summary

The Phase Readiness Middleware is a structural safety component, not a controller.

It exists to answer one question deterministically:

> **"Is the system currently eligible for energy delivery?"**

Everything else is intentionally out of scope.
