#pragma once
#include "resource_manager.h"
#include <thread>

// The CLI or the HTTP controller serializes lifecycle calls. Workers only use the monitor.
class Simulation {
public:
    explicit Simulation(Config config = {}) : config_(config), table_(config) {}
    ~Simulation() { stop(); }
    void start();
    void join();
    void stop();
    bool pause() { return table_.pause(); }
    bool resume() { return table_.resume(); }
    Snapshot snapshot(std::size_t after = 0) const { return table_.snapshot(after); }
    void waitForEvent(std::size_t after) const { table_.waitForEvent(after); }
private:
    Config config_;
    ResourceManager table_;
    std::vector<std::thread> workers_;
};
