#include "simulation.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace std::chrono_literals;
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

// Reconstruct every grant, release and waiting interval independently of snapshots.
void audit(const Snapshot& s) {
    const int n = s.config.count;
    std::deque<int> queue;
    std::vector<int> owners(n, -1), meals(n);
    std::vector<double> hungry(n), acquired(n), waits(n), longest(n);
    double occupied = 0, previousTime = 0;
    int concurrent = 0, peak = 0;
    std::size_t seq = 0;
    for (const auto& e : s.events) {
        check(e.seq == ++seq && e.ms >= previousTime, "event ordering");
        previousTime = e.ms;
        int id = e.id, right = (id + 1) % n;
        if (e.action == "hungry") {
            queue.push_back(id);
            hungry[id] = e.ms;
        } else if (e.action == "acquire") {
            check(!queue.empty() && queue.front() == id, "FIFO order");
            queue.pop_front();
            check(owners[id] == -1 && owners[right] == -1, "adjacent or double ownership");
            owners[id] = owners[right] = id;
            acquired[id] = e.ms;
            waits[id] += e.ms - hungry[id];
            longest[id] = std::max(longest[id], e.ms - hungry[id]);
            peak = std::max(peak, ++concurrent);
        } else if (e.action == "release") {
            check(owners[id] == id && owners[right] == id, "release by non-owner");
            owners[id] = owners[right] = -1;
            occupied += 2 * (e.ms - acquired[id]);
            ++meals[id];
            --concurrent;
        }
    }
    check(s.status == Status::Completed && queue.empty() && s.queue.empty(), "completion");
    check(s.owners == owners && s.eaters == 0 && concurrent == 0, "fork cleanup");
    check(s.peak == peak && peak <= n / 2, "peak concurrency");
    check(s.utilization >= 0 && s.utilization <= 100.000001, "utilization bounds");
    check(std::abs(s.utilization - 100 * occupied / (n * s.elapsed)) < 1e-6, "fork time");
    for (int i = 0; i < n; ++i) {
        const auto& p = s.people[i];
        check(p.state == State::Finished && p.meals == s.config.rounds &&
              p.meals == meals[i] && p.meals == p.grants, "fair completion");
        check(std::abs(waits[i] - p.waitSum) < 1e-6 && longest[i] == p.waitMax, "wait metrics");
    }
}
void controlTests() {
    Simulation sim({5, 3, 0, 30});
    sim.start();
    auto s = sim.snapshot();
    while (!s.eaters) {
        sim.waitForEvent(s.sequence);
        s = sim.snapshot();
        check(s.status == Status::Running, "expected active meal");
    }
    check(sim.pause() && !sim.pause(), "pause");
    auto before = sim.snapshot();
    std::this_thread::sleep_for(75ms);
    auto after = sim.snapshot(before.sequence);
    check(after.events.empty() && after.elapsed == before.elapsed && before.owners == after.owners,
          "pause freezes all workers and the clock");
    check(sim.resume() && !sim.resume(), "resume");
    sim.join();
    audit(sim.snapshot());

    Simulation longRun({10, 100, 0, 60000});
    longRun.start();
    longRun.pause();
    longRun.stop();
    s = longRun.snapshot();
    check(s.status == Status::Stopped && s.eaters == 0 && s.queue.empty(), "cancel while paused");
    check(std::all_of(s.owners.begin(), s.owners.end(), [](int owner){ return owner < 0; }),
          "cancel releases forks");
}
int main() {
    try {
        for (int n : {2, 5, 10}) for (int rounds : {3, 5, 100}) {
            Simulation sim({n, rounds, 0, 1});
            sim.start();
            sim.join();
            audit(sim.snapshot());
            std::cout << "PASS " << n << " philosophers x " << rounds << " rounds\n";
        }
        // A controlled arrival order guarantees that this overlap test is scheduler-independent.
        ResourceManager table({5, 1, 0, 0});
        table.begin();
        for (int id : {0, 2}) check(table.request(id) && table.acquire(id), "disjoint grant");
        check(table.snapshot().eaters == 2, "non-adjacent simultaneous grants");
        table.finishMeal(0);
        table.finishMeal(2);
        table.stop();
        controlTests();
        for (Config cfg : {Config{1,1,0,0}, {11,1,0,0}, {5,0,0,0}, {5,101,0,0},
                           {5,3,-1,0}, {5,3,0,60001}}) {
            bool rejected = false;
            try { Simulation invalid(cfg); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected, "invalid configuration accepted");
        }
        for (const char* value : {"", "-1", "2.5", "5x", "9999999999999"}) {
            bool rejected = false;
            try { integer(value); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected, "invalid integer accepted");
        }
        std::cout << "PASS concurrency, lifecycle and invalid inputs\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
