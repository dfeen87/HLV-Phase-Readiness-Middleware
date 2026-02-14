# HLV Phase Readiness Middleware REST API

**Version:** 1.0.0  
**Protocol:** HTTP/1.1  
**Data Format:** JSON  
**Access:** Read-only (GET endpoints only)

## Overview

The HLV Phase Readiness REST API provides observability-only access to the middleware's internal state. All endpoints are strictly read-only and safe for safety-critical systems.

The API runs in a dedicated thread and does not block the readiness inference loop. All shared data access is mutex-protected for thread safety.

**Base URL:** `http://<host>:8080`  
**Default binding:** `0.0.0.0:8080` (LAN-wide access)

## Safety & Design Principles

- **Read-only:** Only GET requests are supported; no control surfaces
- **Thread-safe:** All data access is mutex-protected
- **Non-blocking:** Runs in dedicated thread, independent of inference loop
- **Observability-first:** Provides snapshots for monitoring and debugging
- **Fail-safe:** Invalid requests return appropriate HTTP error codes

## Endpoints

### GET /health

Health check endpoint for service monitoring.

**Response:**
```json
{
  "status": "ok",
  "service": "HLV Phase Readiness Middleware",
  "version": "1.0.0"
}
```

**Status Codes:**
- `200 OK` - Service is healthy

---

### GET /api/readiness

Returns the current phase readiness value and discrete gate state.

**Response:**
```json
{
  "readiness": 0.850000,
  "gate": "ALLOW",
  "timestamp_s": 123.456789,
  "flags": 0,
  "stability_score": 0.850000
}
```

**Fields:**
- `readiness` (float): Normalized readiness score [0.0–1.0]
  - `1.0` = Fully eligible for energy delivery
  - `0.0` = Blocked/unstable
- `gate` (string): Discrete readiness gate
  - `"ALLOW"` - Energy delivery permitted
  - `"CAUTION"` - Transitional/marginal eligibility
  - `"BLOCK"` - Energy delivery prohibited
- `timestamp_s` (float): System timestamp in seconds
- `flags` (uint32): Bitmask of active condition flags
- `stability_score` (float): Overall stability descriptor [0.0–1.0]

**Status Codes:**
- `200 OK` - Success

---

### GET /api/thermal

Returns thermal state snapshot including temperature, gradients, and trends.

**Response:**
```json
{
  "temperature_C": 25.123456,
  "ambient_C": 22.000000,
  "gradient_C_per_s": 0.012345,
  "trend_C": 0.010234,
  "timestamp_s": 123.456789
}
```

**Fields:**
- `temperature_C` (float|null): Absolute temperature in °C
- `ambient_C` (float|null): Ambient temperature in °C
- `gradient_C_per_s` (float): Instantaneous thermal gradient (dT/dt)
- `trend_C` (float): Smoothed gradient trend (EWMA)
- `timestamp_s` (float): System timestamp in seconds

**Status Codes:**
- `200 OK` - Success

---

### GET /api/history

Returns timestamped history of recent readiness snapshots.

**Response:**
```json
{
  "count": 100,
  "samples": [
    {
      "timestamp_s": 120.0,
      "readiness": 0.850000,
      "gate": "ALLOW",
      "temperature_C": 25.100000,
      "gradient_C_per_s": 0.012000
    },
    {
      "timestamp_s": 120.1,
      "readiness": 0.852000,
      "gate": "ALLOW",
      "temperature_C": 25.102000,
      "gradient_C_per_s": 0.011800
    }
  ]
}
```

**Fields:**
- `count` (int): Number of samples returned
- `samples` (array): Array of historical snapshots
  - `timestamp_s` (float): System timestamp
  - `readiness` (float): Readiness score
  - `gate` (string): Discrete gate state
  - `temperature_C` (float|null): Temperature
  - `gradient_C_per_s` (float): Thermal gradient

**Notes:**
- Returns up to 100 most recent samples
- History size is configurable via `ReadinessAPIState::setMaxHistorySize()`

**Status Codes:**
- `200 OK` - Success

---

### GET /api/phase_context

Returns phase-boundary proximity indicators and hysteresis metrics.

**Response:**
```json
{
  "hysteresis_index": 0.350000,
  "coherence_index": 0.650000,
  "gradient_persistence": 0.010234,
  "gate": "ALLOW",
  "timestamp_s": 123.456789
}
```

**Fields:**
- `hysteresis_index` (float|null): Hysteresis indicator [0.0–1.0]
  - Higher values indicate stronger hysteresis effects
  - `null` if not provided by upstream system
- `coherence_index` (float|null): Coherence indicator [0.0–1.0]
  - Higher values indicate better system coherence
  - `null` if not provided by upstream system
- `gradient_persistence` (float): Current gradient trend
- `gate` (string): Discrete readiness gate
- `timestamp_s` (float): System timestamp

**Status Codes:**
- `200 OK` - Success

---

### GET /api/diagnostics

Returns detailed diagnostic information including flag breakdown and system state.

**Response:**
```json
{
  "flags": 0,
  "flag_meanings": {
    "input_invalid": false,
    "stale_or_nonmono": false,
    "temp_out_of_range": false,
    "gradient_too_high": false,
    "persistent_heating": false,
    "persistent_cooling": false,
    "hysteresis_high": false,
    "coherence_low": false,
    "failsafe_default": false
  },
  "readiness": 0.850000,
  "gate": "ALLOW",
  "stability_score": 0.850000,
  "timestamp_s": 123.456789
}
```

**Fields:**
- `flags` (uint32): Raw bitmask of active condition flags
- `flag_meanings` (object): Human-readable flag breakdown
  - `input_invalid`: Invalid input data detected
  - `stale_or_nonmono`: Stale data or non-monotonic time
  - `temp_out_of_range`: Temperature outside configured limits
  - `gradient_too_high`: Thermal gradient exceeds threshold
  - `persistent_heating`: Sustained heating trend detected
  - `persistent_cooling`: Sustained cooling trend detected
  - `hysteresis_high`: High hysteresis detected
  - `coherence_low`: Low coherence detected
  - `failsafe_default`: Fail-safe mode active
- `readiness` (float): Current readiness score
- `gate` (string): Discrete gate state
- `stability_score` (float): Stability descriptor
- `timestamp_s` (float): System timestamp

**Status Codes:**
- `200 OK` - Success

---

## Error Responses

All error responses follow this format:

```json
{
  "error": {
    "code": 404,
    "message": "Endpoint not found"
  }
}
```

**Common Error Codes:**
- `400 Bad Request` - Invalid HTTP request format
- `404 Not Found` - Endpoint does not exist
- `405 Method Not Allowed` - Only GET requests are supported
- `500 Internal Server Error` - Server-side error

---

## Usage Examples

### Using curl

```bash
# Health check
curl http://localhost:8080/health

# Get current readiness
curl http://localhost:8080/api/readiness

# Get thermal state
curl http://localhost:8080/api/thermal

# Get history
curl http://localhost:8080/api/history

# Get phase context
curl http://localhost:8080/api/phase_context

# Get diagnostics
curl http://localhost:8080/api/diagnostics
```

### Using Python

```python
import requests

base_url = "http://localhost:8080"

# Health check
response = requests.get(f"{base_url}/health")
print(response.json())

# Get readiness
response = requests.get(f"{base_url}/api/readiness")
data = response.json()
print(f"Readiness: {data['readiness']}, Gate: {data['gate']}")

# Get thermal state
response = requests.get(f"{base_url}/api/thermal")
thermal = response.json()
print(f"Temperature: {thermal['temperature_C']}°C")
```

### Using the provided C++ client

```bash
# Build the example client
g++ -std=c++17 -I include -o api_client_example examples/api_client_example.cpp

# Run the client
./api_client_example localhost 8080
```

---

## Integration Guide

### Starting the API Server

```cpp
#include "hlv/rest_api_server.hpp"

// Create API state (shared with readiness loop)
hlv::ReadinessAPIState api_state;

// Configure API server
hlv::RestAPIConfig api_config;
api_config.bind_address = "0.0.0.0";
api_config.port = 8080;

// Create and start server
hlv::RestAPIServer api_server(api_state, api_config);
if (!api_server.start()) {
    // Handle error
}

// Server is now running in dedicated thread
```

### Updating State from Readiness Loop

```cpp
// In your readiness inference loop
PhaseSignals signals = /* ... get sensor data ... */;
PhaseReadinessOutput output = middleware.evaluate(signals);

// Update API state (thread-safe)
api_state.update(signals, output);
```

### Cleanup

```cpp
// Stop server when shutting down
api_server.stop();
```

---

## Thread Safety

All API endpoints are thread-safe and can be queried concurrently. The `ReadinessAPIState` class uses mutexes to protect shared data:

- **Lock-free reads:** Multiple API requests can read simultaneously
- **Safe updates:** Readiness loop updates are synchronized
- **No blocking:** API server runs in dedicated thread

---

## Performance Considerations

- **Lightweight:** POSIX sockets implementation, minimal dependencies
- **Non-blocking:** Does not interfere with readiness inference loop
- **Efficient:** JSON responses are generated on-demand
- **Bounded memory:** History size is configurable and limited

---

## Security Considerations

⚠️ **Important Security Notes:**

1. **No authentication:** This API has no built-in authentication
2. **Read-only by design:** Only GET requests are supported
3. **LAN access:** Default binding to `0.0.0.0` allows LAN-wide access
4. **Production use:** Add authentication/authorization layer if deploying in production
5. **Firewall:** Use firewall rules to restrict access as needed

**Recommendation:** For production deployments, place behind a reverse proxy (nginx, Apache) with authentication and TLS.

---

## Observability Best Practices

### Monitoring

Monitor these key endpoints for system health:

- `/health` - Basic liveness check
- `/api/readiness` - Current readiness state
- `/api/diagnostics` - Detailed flag information

### Alerting

Set up alerts based on:

- `gate == "BLOCK"` - System is blocking energy delivery
- `flags != 0` - Constraint violations detected
- `readiness < threshold` - Readiness below acceptable level

### Logging

Log periodic snapshots for trend analysis:

```bash
# Example: Log readiness every 5 seconds
while true; do
  curl -s http://localhost:8080/api/readiness | jq .
  sleep 5
done
```

---

## Troubleshooting

### Server fails to start

- **Port already in use:** Check if another process is using port 8080
  ```bash
  sudo lsof -i :8080
  ```
- **Permission denied:** Binding to ports < 1024 requires root privileges
- **Address in use:** Wait for socket timeout or use `SO_REUSEADDR`

### Connection refused

- **Server not running:** Verify server started successfully
- **Firewall blocking:** Check firewall rules
- **Wrong address:** Verify host and port configuration

### Empty history

- **No data yet:** History is populated as readiness loop runs
- **Recent reset:** History is cleared on state reset

---

## Changelog

### Version 1.0.0 (Initial Release)

- Implemented core REST API endpoints
- Added thread-safe state management
- Created example server and client
- Documented all endpoints and usage patterns

---

## License

MIT License - See LICENSE file for details.

---

## Support

For issues, questions, or contributions, please refer to the main repository documentation.
