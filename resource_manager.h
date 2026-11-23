#pragma once
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct Config {
    int count = 5, rounds = 3, thinkMs = 700, eatMs = 450;
    void validate() const;
};
int integer(const std::string& text);

enum class State { Ready, Thinking, Hungry, Eating, Finished };
enum class Status { Idle, Running, Paused, Completed, Stopped };
const char* name(State state);
const char* name(Status status);

struct Event {
    std::size_t seq;
    double ms;
    int id; // -1 denotes a session event
    std::string action, message;
};
struct Result {
    State state = State::Ready;
    int meals = 0, grants = 0;
    double waitSum = 0, waitMax = 0, since = 0;
};
struct Snapshot {
    Config config;
    Status status;
    double elapsed, utilization;
    int eaters, peak;
    std::vector<Result> people;
    std::vector<int> owners, queue;
    std::vector<Event> events;
    std::size_t sequence;
};

class ResourceManager {
public:
    explicit ResourceManager(Config config);
    void begin();
    bool pause();
    bool resume();
    void stop();
    bool request(int id);
    bool acquire(int id);
    bool finishMeal(int id);
    Snapshot snapshot(std::size_t after = 0) const;
    void waitForEvent(std::size_t after) const;

private:
    using Clock = std::chrono::steady_clock;
    using Lock = std::unique_lock<std::mutex>;
    double now() const; // active milliseconds, called with mutex held
    bool waitPhase(Lock& lock, double deadline);
    void record(double time, int id, const char* action, const std::string& message);
    std::string forkNames(int id) const;
    const Config config_;
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    Status status_ = Status::Idle;
    Clock::time_point epoch_;
    double accumulated_ = 0, occupied_ = 0;
    int eating_ = 0, peak_ = 0, finished_ = 0;
    std::vector<Result> people_;
    std::vector<int> owners_;
    std::deque<int> fifo_;
    std::vector<Event> history_;
};
