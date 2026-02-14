# HLV Phase Readiness Middleware

**Deterministic, phase-aware readiness inference for energy-coupled physical systems**

## Overview

HLV Phase Readiness is a lightweight middleware layer that infers when a physical system is receptive to energy input, based on thermal state, temporal gradients, and proximity to phase boundaries.

It does **not**:
- control hardware
- simulate physics
- optimize energy flow

Instead, it produces a deterministic **readiness signal** that higher-level systems may use to decide whether now is a stabilizing moment — or a degradation-accelerating one.

This repository exists to explicitly model a reality that many systems depend on implicitly but rarely name:

> **The same energy input can heal or harm a system depending on its phase context.**

## Why This Exists

Most energy-coupled systems — batteries, biological tissue, propulsion stages, structural materials — exhibit nonlinear thermodynamic behavior.

Critical outcomes such as:
- degradation
- hysteresis
- recovery windows
- runaway risk
- efficiency collapse

do **not** arise smoothly from voltage, current, or power alone.

They emerge **near phase boundaries**, where:
- thermal state
- recent history
- temporal gradients

dominate outcomes.

Traditional control systems often:
- apply energy aggressively
- assume monotonic response
- react after instability begins

**HLV Phase Readiness** addresses this gap by answering a simpler, upstream question:

> Is the system currently receptive to energy input?

## Design Philosophy

### What This Middleware Does

✓ Treats thermal state and history as first-class signals  
✓ Infers phase proximity without full physical simulation  
✓ Produces a readiness indicator, not a command  
✓ Enables higher layers to act with temporal intelligence  

### What This Middleware Does Not Do

❌ No CFD  
❌ No electrochemical solvers  
❌ No black-box AI  
❌ No autonomous control  
❌ No direct actuation  

This layer exists **between** sensing and decision-making, not below or above them.

## Theoretical Foundation

Across multiple independent research domains—including the Helix-Light-Vortex framework, protected quantum buffering, detector physics, and closed-loop neurostimulation—there is growing convergence on a shared principle: system response to energy or interaction depends on dynamic state, history, and phase context, not on instantaneous inputs alone.

While HLV Phase Readiness does not model the underlying physical, quantum, or biological mechanisms described in these works, it is motivated by the same constraint they expose—timing determines whether interaction stabilizes a system or accelerates degradation. This convergence reinforces the architectural necessity of a deterministic, domain-agnostic middleware layer that evaluates state eligibility independently of control, optimization, or therapeutic intent.

📄 Foundational Architecture: See WHITEPAPER.md
for the full deterministic phase-readiness middleware design and safety rationale

## Core Concept: Phase Readiness

**Phase readiness** measures whether incremental energy input is likely to:

**Favorable:**
- stabilize the system
- preserve structural coherence
- enable recovery or efficient operation

**Unfavorable:**
- accelerate degradation
- push the system toward instability
- amplify irreversible damage

Readiness is modeled as a time-dependent state, informed by:
- absolute thermal values
- rate of temperature change
- recent thermal history
- gradient persistence
- hysteresis indicators (where available)

The result is a **readiness signal**, not a prediction engine.

## Architectural Position

HLV Phase Readiness is intentionally decoupled from both hardware and control logic.

```
[ Sensors / Telemetry ]
          ↓
[ Phase Readiness Middleware ]   ← (this repository)
          ↓
[ Control / Policy / Optimization ]
          ↓
[ Actuation ]
```

This separation ensures:
- clean responsibility boundaries
- vendor-agnostic integration
- optional adoption
- long-term maintainability

## Intended Outputs

Implementations may expose readiness as:
- a normalized scalar (e.g. 0.0 – 1.0)
- a discrete gate (BLOCK, CAUTION, ALLOW)
- a confidence-weighted state descriptor

The middleware **never decides what action to take** — only whether the timing is favorable.

## REST API for Observability

The middleware includes a read-only HTTP/JSON REST API for observability and monitoring:

- **Strictly read-only:** GET endpoints only, no control surfaces
- **Thread-safe:** Dedicated server thread with mutex-protected data access
- **Non-blocking:** Does not interfere with readiness inference loop
- **LAN-accessible:** Binds to 0.0.0.0:8080 by default

### Available Endpoints

- `GET /health` — Service health check
- `GET /api/readiness` — Current readiness value and gate state
- `GET /api/thermal` — Thermal state and gradients
- `GET /api/history` — Timestamped readiness history
- `GET /api/phase_context` — Phase boundary proximity and hysteresis
- `GET /api/diagnostics` — Detailed system diagnostics with flag breakdown

See [REST_API.md](REST_API.md) for complete documentation and usage examples.

## Intended Use Cases

Although domain-agnostic by design, this middleware naturally applies to:

- **Battery systems** — charge/discharge timing, recovery windows
- **Energy storage & conversion** — thermal saturation avoidance
- **Medical energy delivery** — laser, ultrasound, RF timing
- **Propulsion & power systems** — burn windows, thermal soak limits
- **Advanced materials** — fatigue and phase-transition sensitivity

Any system where **timing matters as much as magnitude** can benefit.

## Non-Goals (Very Important)

This project explicitly avoids:
- high-fidelity physical simulation
- domain-specific heuristics baked into core logic
- hidden optimization objectives
- autonomous decision authority

If you are looking for:
- a controller
- a BMS
- a simulator
- an AI optimizer

**This is not that project.**

## Determinism & Trust

All logic in this middleware is designed to be:
- deterministic
- inspectable
- explainable
- reproducible

No probabilistic learning is required or assumed.

Any statistical smoothing must be:
- bounded
- documented
- optional

**Trust is established through clarity, not opacity.**

## Relationship to Other HLV Projects

This repository is intentionally separate from:
- HLV hardware adapters
- battery-specific middleware
- control or optimization layers

Those systems may **consume** phase readiness outputs —  
this repo **depends on none of them**.

Separation ensures:
- reuse across domains
- minimal churn
- architectural clarity

## Project Status

**Early architectural phase.**

Initial focus:
- defining clean interfaces
- documenting assumptions
- establishing non-goals
- preventing overreach

Functionality will grow slowly and deliberately.

## Building and Testing

### Prerequisites

- C++17 compatible compiler (g++ 7.0+ or clang++ 5.0+)
- POSIX-compliant system (Linux, macOS, BSD)
- pthread support for REST API server

### Building the Examples

```bash
# Build the core library tests
g++ -std=c++17 -I include -o phase_readiness_tests \
    tests/phase_readiness_tests.cpp src/phase_readiness.cpp

# Build REST API tests
g++ -std=c++17 -I include -pthread -o rest_api_tests \
    tests/rest_api_tests.cpp src/phase_readiness.cpp src/rest_api_server.cpp

# Build API server example
g++ -std=c++17 -I include -pthread -o api_server_example \
    examples/api_server_example.cpp src/phase_readiness.cpp src/rest_api_server.cpp

# Build API client example
g++ -std=c++17 -I include -o api_client_example \
    examples/api_client_example.cpp
```

### Running Tests

```bash
# Run core middleware tests
./phase_readiness_tests

# Run REST API tests
./rest_api_tests
```

### Running the REST API Server

```bash
# Start the example server (binds to 0.0.0.0:8080)
./api_server_example

# In another terminal, test the API
curl http://localhost:8080/health
curl http://localhost:8080/api/readiness
curl http://localhost:8080/api/thermal

# Or use the example client
./api_client_example localhost 8080
```

See [REST_API.md](REST_API.md) for complete API documentation.

## License

MIT License — permissive, transparent, and intended for both research and production exploration.

## Final Note

**Timing is a physical property, not just a scheduling concern.**

By making phase readiness explicit, this middleware gives higher-level systems the opportunity to act with:
- restraint
- intelligence
- respect for underlying physics

— without pretending to simulate it.
