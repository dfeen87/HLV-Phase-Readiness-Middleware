#pragma once

// Read-only HTTP/JSON REST API for HLV Phase Readiness Middleware
//
// SAFETY PRINCIPLES:
// - Strictly read-only (GET endpoints only)
// - Thread-safe with mutex-protected shared data
// - Runs in dedicated thread, non-blocking to readiness loop
// - Lightweight POSIX sockets implementation
// - No control surfaces, observability only

#include "hlv/phase_readiness.hpp"
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hlv {

// Timestamped snapshot for history tracking
struct ReadinessSnapshot {
  std::chrono::steady_clock::time_point timestamp{};
  double t_s = 0.0;
  double readiness = 0.0;
  Gate gate = Gate::BLOCK;
  uint32_t flags = 0;
  double temp_C = std::numeric_limits<double>::quiet_NaN();
  double temp_ambient_C = std::numeric_limits<double>::quiet_NaN();
  double dTdt_C_per_s = 0.0;
  double trend_C = 0.0;
  double stability_score = 0.0;
  double hysteresis_index = std::numeric_limits<double>::quiet_NaN();
  double coherence_index = std::numeric_limits<double>::quiet_NaN();
};

// Thread-safe shared state for API server
class ReadinessAPIState {
public:
  ReadinessAPIState();
  
  // Update current state (called by readiness inference loop)
  void update(const PhaseSignals& signals, const PhaseReadinessOutput& output);
  
  // Read-only access methods (called by API endpoints)
  ReadinessSnapshot getCurrentSnapshot() const;
  std::vector<ReadinessSnapshot> getHistory(size_t max_count) const;
  
  // Configuration
  void setMaxHistorySize(size_t size);
  
private:
  mutable std::mutex mutex_;
  ReadinessSnapshot current_;
  std::deque<ReadinessSnapshot> history_;
  size_t max_history_size_;
};

// Configuration for REST API server
struct RestAPIConfig {
  std::string bind_address = "0.0.0.0";
  uint16_t port = 8080;
  int listen_backlog = 10;
  int socket_timeout_ms = 5000;
};

// REST API Server
// Runs in dedicated thread, provides read-only observability
class RestAPIServer {
public:
  explicit RestAPIServer(ReadinessAPIState& state, RestAPIConfig config = RestAPIConfig{});
  ~RestAPIServer();
  
  // Start server thread
  bool start();
  
  // Stop server thread
  void stop();
  
  // Check if server is running
  bool isRunning() const;
  
private:
  ReadinessAPIState& state_;
  RestAPIConfig config_;
  std::atomic<bool> running_;
  std::atomic<bool> should_stop_;
  std::thread server_thread_;
  int server_socket_;
  
  // Server thread entry point
  void serverLoop();
  
  // Handle single client connection
  void handleClient(int client_socket);
  
  // HTTP request parsing
  struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
  };
  
  bool parseRequest(const std::string& request, HttpRequest& parsed);
  
  // Endpoint handlers
  std::string handleHealth();
  std::string handleReadiness();
  std::string handleThermal();
  std::string handleHistory();
  std::string handlePhaseContext();
  std::string handleDiagnostics();
  
  // Response generation
  std::string makeHttpResponse(int status_code, const std::string& status_text,
                               const std::string& body, const std::string& content_type = "application/json");
  std::string makeJsonError(int code, const std::string& message);
  
  // Utility
  static std::string jsonEscape(const std::string& s);
  static void writeJsonDouble(std::ostringstream& json, const char* key, double value, bool comma = true);
  static std::string formatTimestamp(const std::chrono::steady_clock::time_point& tp);
  bool sendAll(int sock, const std::string& data);
};

} // namespace hlv
