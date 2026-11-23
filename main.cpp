#include "simulation.h"
#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
    try {
        if (argc > 5) throw std::invalid_argument("Too many arguments.");
        Config c;
        int* fields[] = {&c.count, &c.rounds, &c.thinkMs, &c.eatMs};
        for (int i = 1; i < argc; ++i) *fields[i - 1] = integer(argv[i]);
        Simulation sim(c);
        std::cout << "Dining Philosophers | strict FIFO | " << c.count << " philosophers | "
                  << c.rounds << " rounds | think " << c.thinkMs << " ms | eat " << c.eatMs << " ms\n";
        sim.start();
        std::size_t cursor = 0;
        std::cout << std::fixed << std::setprecision(2);
        for (;;) {
            sim.waitForEvent(cursor);
            const auto s = sim.snapshot(cursor);
            for (const auto& event : s.events) {
                std::cout << "[" << event.ms << " ms] ";
                if (event.id >= 0) std::cout << "P" << event.id << " ";
                std::cout << event.message << '\n';
            }
            cursor = s.sequence;
            if (s.status != Status::Completed) continue;
            sim.join();
            std::cout << "\nPhilosopher   Meals   Avg wait (ms)   Max wait (ms)\n";
            for (int i = 0; i < c.count; ++i) {
                const auto& p = s.people[i];
                std::cout << "P" << i << std::setw(16) << p.meals << std::setw(16)
                          << p.waitSum / p.grants << std::setw(16) << p.waitMax << '\n';
            }
            std::cout << "Total duration: " << s.elapsed << " ms\nFork utilization: "
                      << s.utilization << "%\nMaximum simultaneous eaters: " << s.peak << '\n';
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\nUsage: dining [philosophers [rounds [think_ms [eat_ms]]]]\n";
        return 1;
    }
}
