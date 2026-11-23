#include "simulation.h"
#include "philosopher.h"
#include <functional>

void Simulation::start() {
    table_.begin();
    try {
        for (int id = 0; id < config_.count; ++id)
            workers_.emplace_back(dine, std::ref(table_), id, config_.rounds);
    } catch (...) {
        stop();
        throw;
    }
}
void Simulation::join() {
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
}
void Simulation::stop() {
    table_.stop();
    join();
}
