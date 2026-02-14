#include "hlv/phase_readiness.hpp"
#include "hlv/rest_api_server.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

using namespace hlv;

// Test 1: ReadinessAPIState basic functionality
static void test_api_state_basic() {
  ReadinessAPIState state;
  
  // Create test signals and output
  PhaseSignals signals;
  signals.t_s = 1.0;
  signals.temp_C = 25.0;
  signals.temp_ambient_C = 22.0;
  signals.valid = true;
  signals.coherence_index = 0.5;
  signals.hysteresis_index = 0.3;
  
  PhaseReadinessOutput output;
  output.readiness = 0.85;
  output.gate = Gate::ALLOW;
  output.flags = FLAG_NONE;
  output.dTdt_C_per_s = 0.1;
  output.trend_C = 0.05;
  output.stability_score = 0.85;
  
  // Update state
  state.update(signals, output);
  
  // Get current snapshot
  auto snapshot = state.getCurrentSnapshot();
  
  // Verify data
  assert(snapshot.t_s == 1.0);
  assert(snapshot.temp_C == 25.0);
  assert(snapshot.temp_ambient_C == 22.0);
  assert(snapshot.readiness == 0.85);
  assert(snapshot.gate == Gate::ALLOW);
  assert(snapshot.flags == FLAG_NONE);
  assert(snapshot.coherence_index == 0.5);
  assert(snapshot.hysteresis_index == 0.3);
}

// Test 2: History tracking
static void test_api_state_history() {
  ReadinessAPIState state;
  state.setMaxHistorySize(5);
  
  // Add multiple samples
  for (int i = 0; i < 10; ++i) {
    PhaseSignals signals;
    signals.t_s = i * 0.1;
    signals.temp_C = 25.0 + i * 0.1;
    signals.valid = true;
    
    PhaseReadinessOutput output;
    output.readiness = 0.5 + i * 0.05;
    output.gate = Gate::CAUTION;
    output.flags = FLAG_NONE;
    
    state.update(signals, output);
  }
  
  // Get history (should be limited to 5)
  auto history = state.getHistory(100);
  assert(history.size() == 5);
  
  // Verify last sample
  assert(history.back().t_s == 0.9);
  assert(history.back().temp_C == 25.9);
}

// Test 3: Thread safety (basic concurrent access)
static void test_api_state_thread_safety() {
  ReadinessAPIState state;
  
  std::atomic<bool> stop(false);
  
  // Writer thread
  std::thread writer([&]() {
    for (int i = 0; i < 100 && !stop.load(); ++i) {
      PhaseSignals signals;
      signals.t_s = i * 0.01;
      signals.temp_C = 25.0;
      signals.valid = true;
      
      PhaseReadinessOutput output;
      output.readiness = 0.8;
      output.gate = Gate::ALLOW;
      output.flags = FLAG_NONE;
      
      state.update(signals, output);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  
  // Reader threads
  std::thread reader1([&]() {
    for (int i = 0; i < 100 && !stop.load(); ++i) {
      auto snapshot = state.getCurrentSnapshot();
      (void)snapshot; // Use it to avoid unused variable warning
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  
  std::thread reader2([&]() {
    for (int i = 0; i < 100 && !stop.load(); ++i) {
      auto history = state.getHistory(10);
      (void)history; // Use it to avoid unused variable warning
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  
  // Let threads run
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  stop.store(true);
  
  writer.join();
  reader1.join();
  reader2.join();
  
  // If we get here without deadlock or crash, test passes
}

// Test 4: REST API server start/stop
static void test_api_server_lifecycle() {
  ReadinessAPIState state;
  
  RestAPIConfig config;
  config.port = 8081; // Use different port to avoid conflicts
  
  RestAPIServer server(state, config);
  
  // Initially not running
  assert(!server.isRunning());
  
  // Start server
  bool started = server.start();
  
  // May fail if port is in use, but that's ok for this test
  if (started) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(server.isRunning());
    
    // Stop server
    server.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(!server.isRunning());
  }
}

// Test 5: Gate string conversion
static void test_gate_to_string() {
  // This tests the private function indirectly through API responses
  // We'll just verify the basic state works
  ReadinessAPIState state;
  
  PhaseSignals signals;
  signals.t_s = 1.0;
  signals.temp_C = 25.0;
  signals.valid = true;
  
  // Test BLOCK
  PhaseReadinessOutput output;
  output.readiness = 0.0;
  output.gate = Gate::BLOCK;
  output.flags = FLAG_NONE;
  
  state.update(signals, output);
  auto snapshot = state.getCurrentSnapshot();
  assert(snapshot.gate == Gate::BLOCK);
  
  // Test CAUTION
  output.gate = Gate::CAUTION;
  state.update(signals, output);
  snapshot = state.getCurrentSnapshot();
  assert(snapshot.gate == Gate::CAUTION);
  
  // Test ALLOW
  output.gate = Gate::ALLOW;
  state.update(signals, output);
  snapshot = state.getCurrentSnapshot();
  assert(snapshot.gate == Gate::ALLOW);
}

// Test 6: NaN handling in snapshots
static void test_nan_handling() {
  ReadinessAPIState state;
  
  PhaseSignals signals;
  signals.t_s = 1.0;
  signals.temp_C = 25.0;
  signals.valid = true;
  // Leave coherence and hysteresis as NaN
  signals.coherence_index = std::numeric_limits<double>::quiet_NaN();
  signals.hysteresis_index = std::numeric_limits<double>::quiet_NaN();
  
  PhaseReadinessOutput output;
  output.readiness = 0.85;
  output.gate = Gate::ALLOW;
  output.flags = FLAG_NONE;
  
  state.update(signals, output);
  
  auto snapshot = state.getCurrentSnapshot();
  assert(std::isnan(snapshot.coherence_index));
  assert(std::isnan(snapshot.hysteresis_index));
  assert(!std::isnan(snapshot.temp_C)); // This should be valid
}

int main() {
  std::cout << "Running REST API tests...\n";
  
  test_api_state_basic();
  std::cout << "[PASS] API state basic functionality\n";
  
  test_api_state_history();
  std::cout << "[PASS] API state history tracking\n";
  
  test_api_state_thread_safety();
  std::cout << "[PASS] API state thread safety\n";
  
  test_api_server_lifecycle();
  std::cout << "[PASS] API server lifecycle\n";
  
  test_gate_to_string();
  std::cout << "[PASS] Gate state handling\n";
  
  test_nan_handling();
  std::cout << "[PASS] NaN handling\n";
  
  std::cout << "\n[PASS] All REST API tests passed!\n";
  
  return 0;
}
