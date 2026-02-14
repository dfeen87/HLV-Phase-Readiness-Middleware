// Example HTTP client for HLV Phase Readiness REST API
// Demonstrates how to query the API endpoints

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

// Simple HTTP GET request
std::string httpGet(const std::string& host, int port, const std::string& path) {
  // Create socket
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    return "ERROR: Failed to create socket";
  }
  
  // Set timeout
  struct timeval timeout;
  timeout.tv_sec = 5;
  timeout.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  
  // Connect
  struct sockaddr_in server_addr;
  std::memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  
  if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
    close(sock);
    return "ERROR: Invalid address";
  }
  
  if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
    close(sock);
    return "ERROR: Connection failed";
  }
  
  // Send HTTP GET request
  std::ostringstream request;
  request << "GET " << path << " HTTP/1.1\r\n";
  request << "Host: " << host << "\r\n";
  request << "Connection: close\r\n";
  request << "\r\n";
  
  std::string req_str = request.str();
  send(sock, req_str.c_str(), req_str.length(), 0);
  
  // Read response
  std::string response;
  char buffer[4096];
  ssize_t bytes_read;
  
  while ((bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
    buffer[bytes_read] = '\0';
    response += buffer;
  }
  
  close(sock);
  
  // Extract body (after \r\n\r\n)
  size_t body_start = response.find("\r\n\r\n");
  if (body_start != std::string::npos) {
    return response.substr(body_start + 4);
  }
  
  return response;
}

int main(int argc, char* argv[]) {
  std::string host = "127.0.0.1";
  int port = 8080;
  
  // Allow override from command line
  if (argc > 1) {
    host = argv[1];
  }
  if (argc > 2) {
    port = std::stoi(argv[2]);
  }
  
  std::cout << "HLV Phase Readiness REST API Client Example\n";
  std::cout << "============================================\n";
  std::cout << "Connecting to " << host << ":" << port << "\n\n";
  
  // Test all endpoints
  std::cout << "=== GET /health ===\n";
  std::cout << httpGet(host, port, "/health") << "\n\n";
  
  std::cout << "=== GET /api/readiness ===\n";
  std::cout << httpGet(host, port, "/api/readiness") << "\n\n";
  
  std::cout << "=== GET /api/thermal ===\n";
  std::cout << httpGet(host, port, "/api/thermal") << "\n\n";
  
  std::cout << "=== GET /api/phase_context ===\n";
  std::cout << httpGet(host, port, "/api/phase_context") << "\n\n";
  
  std::cout << "=== GET /api/diagnostics ===\n";
  std::cout << httpGet(host, port, "/api/diagnostics") << "\n\n";
  
  std::cout << "=== GET /api/history (last 5 samples) ===\n";
  std::string history = httpGet(host, port, "/api/history");
  // For brevity, just show first 500 chars
  if (history.length() > 500) {
    std::cout << history.substr(0, 500) << "\n... (truncated)\n\n";
  } else {
    std::cout << history << "\n\n";
  }
  
  std::cout << "Client test complete!\n";
  
  return 0;
}
