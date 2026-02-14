// Example server demonstrating HLV Phase Readiness REST API
// This example simulates a readiness inference loop and exposes the data via REST API

#include "hlv/phase_readiness.hpp"
#include "hlv/rest_api_server.hpp"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <thread>

using namespace hlv;

int main() {
  std::cout << "HLV Phase Readiness REST API Server Example\n";
  std::cout << "============================================\n\n";
  
  // Create middleware instance
  PhaseReadinessConfig config;
  config.temp_min_C = 15.0;
  config.temp_max_C = 45.0;
  config.max_abs_dTdt_C_per_s = 0.25;
  config.persistence_s = 3.0;
  
  PhaseReadinessMiddleware middleware(config);
  
  // Create API state
  ReadinessAPIState api_state;
  api_state.setMaxHistorySize(100);
  
  // Create and start REST API server
  RestAPIConfig api_config;
  api_config.bind_address = "0.0.0.0";
  api_config.port = 8080;
  
  RestAPIServer api_server(api_state, api_config);
  
  std::cout << "Starting REST API server on " << api_config.bind_address 
            << ":" << api_config.port << "...\n";
  
  if (!api_server.start()) {
    std::cerr << "Failed to start REST API server!\n";
    std::cerr << "Make sure port " << api_config.port << " is not already in use.\n";
    return 1;
  }
  
  std::cout << "REST API server started successfully!\n\n";
  std::cout << "Available endpoints:\n";
  std::cout << "  GET http://localhost:8080/health\n";
  std::cout << "  GET http://localhost:8080/api/readiness\n";
  std::cout << "  GET http://localhost:8080/api/thermal\n";
  std::cout << "  GET http://localhost:8080/api/history\n";
  std::cout << "  GET http://localhost:8080/api/phase_context\n";
  std::cout << "  GET http://localhost:8080/api/diagnostics\n";
  std::cout << "\nPress Ctrl+C to stop.\n\n";
  
  // Simulated readiness inference loop
  double time_s = 0.0;
  double base_temp = 25.0;
  int cycle = 0;
  
  while (true) {
    // Simulate temperature variations
    double temp_variation = 2.0 * std::sin(time_s * 0.5);
    double temp_C = base_temp + temp_variation;
    
    // Create input signals
    PhaseSignals signals;
    signals.t_s = time_s;
    signals.temp_C = temp_C;
    signals.temp_ambient_C = 22.0;
    signals.valid = true;
    
    // Simulate optional indicators (vary over time)
    if (cycle % 10 < 7) {
      signals.coherence_index = 0.5 + 0.3 * std::sin(time_s * 0.3);
      signals.hysteresis_index = 0.3 + 0.2 * std::sin(time_s * 0.2);
    }
    
    // Evaluate readiness
    PhaseReadinessOutput output = middleware.evaluate(signals);
    
    // Update API state
    api_state.update(signals, output);
    
    // Log to console every 10 cycles
    if (cycle % 10 == 0) {
      std::cout << "[t=" << std::fixed << std::setprecision(1) << time_s << "s] ";
      std::cout << "T=" << std::setprecision(2) << temp_C << "°C, ";
      std::cout << "R=" << std::setprecision(3) << output.readiness << ", ";
      
      switch (output.gate) {
        case Gate::BLOCK: std::cout << "Gate=BLOCK"; break;
        case Gate::CAUTION: std::cout << "Gate=CAUTION"; break;
        case Gate::ALLOW: std::cout << "Gate=ALLOW"; break;
      }
      
      if (output.flags != FLAG_NONE) {
        std::cout << " [flags=" << output.flags << "]";
      }
      std::cout << "\n";
    }
    
    // Sleep for simulation timestep
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    time_s += 0.1;
    cycle++;
  }
  
  // Cleanup (unreachable in this example)
  api_server.stop();
  
  return 0;
}
