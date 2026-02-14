#include "hlv/rest_api_server.hpp"

#include <arpa/inet.h>
#include <cmath>
#include <cstring>
#include <errno.h>
#include <iomanip>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace hlv {

// -----------------------------------------------------------------------------
// ReadinessAPIState Implementation
// -----------------------------------------------------------------------------

ReadinessAPIState::ReadinessAPIState()
    : max_history_size_(100)
{
  current_.timestamp = std::chrono::steady_clock::now();
  current_.t_s = 0.0;
  current_.readiness = 0.0;
  current_.gate = Gate::BLOCK;
  current_.flags = FLAG_NONE;
  current_.temp_C = std::numeric_limits<double>::quiet_NaN();
  current_.temp_ambient_C = std::numeric_limits<double>::quiet_NaN();
  current_.dTdt_C_per_s = 0.0;
  current_.trend_C = 0.0;
  current_.stability_score = 0.0;
  current_.hysteresis_index = std::numeric_limits<double>::quiet_NaN();
  current_.coherence_index = std::numeric_limits<double>::quiet_NaN();
}

void ReadinessAPIState::update(const PhaseSignals& signals, const PhaseReadinessOutput& output) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  current_.timestamp = std::chrono::steady_clock::now();
  current_.t_s = signals.t_s;
  current_.readiness = output.readiness;
  current_.gate = output.gate;
  current_.flags = output.flags;
  current_.temp_C = signals.temp_C;
  current_.temp_ambient_C = signals.temp_ambient_C;
  current_.dTdt_C_per_s = output.dTdt_C_per_s;
  current_.trend_C = output.trend_C;
  current_.stability_score = output.stability_score;
  current_.hysteresis_index = signals.hysteresis_index;
  current_.coherence_index = signals.coherence_index;
  
  // Add to history
  history_.push_back(current_);
  
  // Trim history if needed
  while (history_.size() > max_history_size_) {
    history_.pop_front();
  }
}

ReadinessSnapshot ReadinessAPIState::getCurrentSnapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_;
}

std::vector<ReadinessSnapshot> ReadinessAPIState::getHistory(size_t max_count) const {
  std::lock_guard<std::mutex> lock(mutex_);
  
  size_t count = std::min(max_count, history_.size());
  std::vector<ReadinessSnapshot> result;
  result.reserve(count);
  
  // Get last N samples
  auto it = history_.end() - count;
  for (; it != history_.end(); ++it) {
    result.push_back(*it);
  }
  
  return result;
}

void ReadinessAPIState::setMaxHistorySize(size_t size) {
  std::lock_guard<std::mutex> lock(mutex_);
  max_history_size_ = size;
  
  // Trim if needed
  while (history_.size() > max_history_size_) {
    history_.pop_front();
  }
}

// -----------------------------------------------------------------------------
// RestAPIServer Implementation
// -----------------------------------------------------------------------------

RestAPIServer::RestAPIServer(ReadinessAPIState& state, RestAPIConfig config)
    : state_(state)
    , config_(config)
    , running_(false)
    , should_stop_(false)
    , server_socket_(-1)
{}

RestAPIServer::~RestAPIServer() {
  stop();
}

bool RestAPIServer::start() {
  if (running_.load()) {
    return false; // Already running
  }
  
  // Create socket
  server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket_ < 0) {
    return false;
  }
  
  // Set socket options
  int opt = 1;
  if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    close(server_socket_);
    server_socket_ = -1;
    return false;
  }
  
  // Set timeout
  struct timeval timeout;
  timeout.tv_sec = config_.socket_timeout_ms / 1000;
  timeout.tv_usec = (config_.socket_timeout_ms % 1000) * 1000;
  setsockopt(server_socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  
  // Bind
  struct sockaddr_in address;
  std::memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(config_.port);
  
  if (config_.bind_address == "0.0.0.0") {
    address.sin_addr.s_addr = INADDR_ANY;
  } else {
    if (inet_pton(AF_INET, config_.bind_address.c_str(), &address.sin_addr) <= 0) {
      close(server_socket_);
      server_socket_ = -1;
      return false;
    }
  }
  
  if (bind(server_socket_, (struct sockaddr*)&address, sizeof(address)) < 0) {
    close(server_socket_);
    server_socket_ = -1;
    return false;
  }
  
  // Listen
  if (listen(server_socket_, config_.listen_backlog) < 0) {
    close(server_socket_);
    server_socket_ = -1;
    return false;
  }
  
  // Start server thread
  should_stop_.store(false);
  server_thread_ = std::thread(&RestAPIServer::serverLoop, this);
  
  return true;
}

void RestAPIServer::stop() {
  if (!running_.load()) {
    return;
  }
  
  should_stop_.store(true);
  
  if (server_thread_.joinable()) {
    server_thread_.join();
  }
  
  if (server_socket_ >= 0) {
    close(server_socket_);
    server_socket_ = -1;
  }
  
  running_.store(false);
}

bool RestAPIServer::isRunning() const {
  return running_.load();
}

void RestAPIServer::serverLoop() {
  running_.store(true);
  
  while (!should_stop_.load()) {
    struct sockaddr_in client_address;
    socklen_t client_len = sizeof(client_address);
    
    int client_socket = accept(server_socket_, (struct sockaddr*)&client_address, &client_len);
    
    if (client_socket < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Timeout, continue
        continue;
      }
      // Error or shutdown
      break;
    }
    
    // Handle client request
    handleClient(client_socket);
    close(client_socket);
  }
  
  running_.store(false);
}

void RestAPIServer::handleClient(int client_socket) {
  char buffer[4096];
  ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
  
  if (bytes_read <= 0) {
    return;
  }
  
  buffer[bytes_read] = '\0';
  std::string request(buffer);
  
  // Parse HTTP request
  HttpRequest parsed;
  if (!parseRequest(request, parsed)) {
    std::string response = makeHttpResponse(400, "Bad Request", 
                                           makeJsonError(400, "Invalid HTTP request"),
                                           "application/json");
    send(client_socket, response.c_str(), response.length(), 0);
    return;
  }
  
  // Only allow GET requests
  if (parsed.method != "GET") {
    std::string response = makeHttpResponse(405, "Method Not Allowed",
                                           makeJsonError(405, "Only GET requests are allowed"),
                                           "application/json");
    send(client_socket, response.c_str(), response.length(), 0);
    return;
  }
  
  // Route to appropriate handler
  std::string body;
  int status_code = 200;
  std::string status_text = "OK";
  
  try {
    if (parsed.path == "/health") {
      body = handleHealth();
    } else if (parsed.path == "/api/readiness") {
      body = handleReadiness();
    } else if (parsed.path == "/api/thermal") {
      body = handleThermal();
    } else if (parsed.path == "/api/history") {
      body = handleHistory();
    } else if (parsed.path == "/api/phase_context") {
      body = handlePhaseContext();
    } else if (parsed.path == "/api/diagnostics") {
      body = handleDiagnostics();
    } else {
      status_code = 404;
      status_text = "Not Found";
      body = makeJsonError(404, "Endpoint not found");
    }
  } catch (const std::exception& e) {
    status_code = 500;
    status_text = "Internal Server Error";
    body = makeJsonError(500, std::string("Internal error: ") + e.what());
  }
  
  std::string response = makeHttpResponse(status_code, status_text, body, "application/json");
  send(client_socket, response.c_str(), response.length(), 0);
}

bool RestAPIServer::parseRequest(const std::string& request, HttpRequest& parsed) {
  std::istringstream iss(request);
  std::string line;
  
  if (!std::getline(iss, line)) {
    return false;
  }
  
  // Remove trailing \r if present
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  
  std::istringstream line_stream(line);
  if (!(line_stream >> parsed.method >> parsed.path >> parsed.version)) {
    return false;
  }
  
  return true;
}

std::string RestAPIServer::handleHealth() {
  std::ostringstream json;
  json << "{\n";
  json << "  \"status\": \"ok\",\n";
  json << "  \"service\": \"HLV Phase Readiness Middleware\",\n";
  json << "  \"version\": \"1.0.0\"\n";
  json << "}";
  return json.str();
}

std::string RestAPIServer::handleReadiness() {
  auto snapshot = state_.getCurrentSnapshot();
  
  std::ostringstream json;
  json << std::fixed << std::setprecision(6);
  json << "{\n";
  json << "  \"readiness\": " << snapshot.readiness << ",\n";
  json << "  \"gate\": \"" << gateToString(snapshot.gate) << "\",\n";
  json << "  \"timestamp_s\": " << snapshot.t_s << ",\n";
  json << "  \"flags\": " << snapshot.flags << ",\n";
  json << "  \"stability_score\": " << snapshot.stability_score << "\n";
  json << "}";
  return json.str();
}

std::string RestAPIServer::handleThermal() {
  auto snapshot = state_.getCurrentSnapshot();
  
  std::ostringstream json;
  json << std::fixed << std::setprecision(6);
  json << "{\n";
  json << "  \"temperature_C\": " << (std::isfinite(snapshot.temp_C) ? std::to_string(snapshot.temp_C) : "null") << ",\n";
  json << "  \"ambient_C\": " << (std::isfinite(snapshot.temp_ambient_C) ? std::to_string(snapshot.temp_ambient_C) : "null") << ",\n";
  json << "  \"gradient_C_per_s\": " << snapshot.dTdt_C_per_s << ",\n";
  json << "  \"trend_C\": " << snapshot.trend_C << ",\n";
  json << "  \"timestamp_s\": " << snapshot.t_s << "\n";
  json << "}";
  return json.str();
}

std::string RestAPIServer::handleHistory() {
  auto history = state_.getHistory(100); // Last 100 samples
  
  std::ostringstream json;
  json << std::fixed << std::setprecision(6);
  json << "{\n";
  json << "  \"count\": " << history.size() << ",\n";
  json << "  \"samples\": [\n";
  
  for (size_t i = 0; i < history.size(); ++i) {
    const auto& s = history[i];
    json << "    {\n";
    json << "      \"timestamp_s\": " << s.t_s << ",\n";
    json << "      \"readiness\": " << s.readiness << ",\n";
    json << "      \"gate\": \"" << gateToString(s.gate) << "\",\n";
    json << "      \"temperature_C\": " << (std::isfinite(s.temp_C) ? std::to_string(s.temp_C) : "null") << ",\n";
    json << "      \"gradient_C_per_s\": " << s.dTdt_C_per_s << "\n";
    json << "    }";
    if (i < history.size() - 1) {
      json << ",";
    }
    json << "\n";
  }
  
  json << "  ]\n";
  json << "}";
  return json.str();
}

std::string RestAPIServer::handlePhaseContext() {
  auto snapshot = state_.getCurrentSnapshot();
  
  std::ostringstream json;
  json << std::fixed << std::setprecision(6);
  json << "{\n";
  json << "  \"hysteresis_index\": " << (std::isfinite(snapshot.hysteresis_index) ? std::to_string(snapshot.hysteresis_index) : "null") << ",\n";
  json << "  \"coherence_index\": " << (std::isfinite(snapshot.coherence_index) ? std::to_string(snapshot.coherence_index) : "null") << ",\n";
  json << "  \"gradient_persistence\": " << snapshot.trend_C << ",\n";
  json << "  \"gate\": \"" << gateToString(snapshot.gate) << "\",\n";
  json << "  \"timestamp_s\": " << snapshot.t_s << "\n";
  json << "}";
  return json.str();
}

std::string RestAPIServer::handleDiagnostics() {
  auto snapshot = state_.getCurrentSnapshot();
  
  std::ostringstream json;
  json << std::fixed << std::setprecision(6);
  json << "{\n";
  json << "  \"flags\": " << snapshot.flags << ",\n";
  json << "  \"flag_meanings\": {\n";
  json << "    \"input_invalid\": " << ((snapshot.flags & FLAG_INPUT_INVALID) ? "true" : "false") << ",\n";
  json << "    \"stale_or_nonmono\": " << ((snapshot.flags & FLAG_STALE_OR_NONMONO) ? "true" : "false") << ",\n";
  json << "    \"temp_out_of_range\": " << ((snapshot.flags & FLAG_TEMP_OUT_OF_RANGE) ? "true" : "false") << ",\n";
  json << "    \"gradient_too_high\": " << ((snapshot.flags & FLAG_GRADIENT_TOO_HIGH) ? "true" : "false") << ",\n";
  json << "    \"persistent_heating\": " << ((snapshot.flags & FLAG_PERSISTENT_HEATING) ? "true" : "false") << ",\n";
  json << "    \"persistent_cooling\": " << ((snapshot.flags & FLAG_PERSISTENT_COOLING) ? "true" : "false") << ",\n";
  json << "    \"hysteresis_high\": " << ((snapshot.flags & FLAG_HYSTERESIS_HIGH) ? "true" : "false") << ",\n";
  json << "    \"coherence_low\": " << ((snapshot.flags & FLAG_COHERENCE_LOW) ? "true" : "false") << ",\n";
  json << "    \"failsafe_default\": " << ((snapshot.flags & FLAG_FAILSAFE_DEFAULT) ? "true" : "false") << "\n";
  json << "  },\n";
  json << "  \"readiness\": " << snapshot.readiness << ",\n";
  json << "  \"gate\": \"" << gateToString(snapshot.gate) << "\",\n";
  json << "  \"stability_score\": " << snapshot.stability_score << ",\n";
  json << "  \"timestamp_s\": " << snapshot.t_s << "\n";
  json << "}";
  return json.str();
}

std::string RestAPIServer::makeHttpResponse(int status_code, const std::string& status_text,
                                            const std::string& body, const std::string& content_type) {
  std::ostringstream response;
  response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
  response << "Content-Type: " << content_type << "\r\n";
  response << "Content-Length: " << body.length() << "\r\n";
  response << "Connection: close\r\n";
  response << "\r\n";
  response << body;
  return response.str();
}

std::string RestAPIServer::makeJsonError(int code, const std::string& message) {
  std::ostringstream json;
  json << "{\n";
  json << "  \"error\": {\n";
  json << "    \"code\": " << code << ",\n";
  json << "    \"message\": \"" << message << "\"\n";
  json << "  }\n";
  json << "}";
  return json.str();
}

std::string RestAPIServer::gateToString(Gate g) {
  switch (g) {
    case Gate::BLOCK: return "BLOCK";
    case Gate::CAUTION: return "CAUTION";
    case Gate::ALLOW: return "ALLOW";
    default: return "UNKNOWN";
  }
}

std::string RestAPIServer::formatTimestamp(const std::chrono::steady_clock::time_point& tp) {
  auto duration = tp.time_since_epoch();
  auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
  
  std::ostringstream oss;
  oss << seconds;
  return oss.str();
}

} // namespace hlv
