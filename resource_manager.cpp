#include "resource_manager.h"
#include <algorithm>
#include <stdexcept>

void Config::validate() const {
    if (count < 2 || count > 10 || rounds < 1 || rounds > 100 ||
        thinkMs < 0 || thinkMs > 60000 || eatMs < 0 || eatMs > 60000)
        throw std::invalid_argument("Use 2-10 philosophers, 1-100 rounds and 0-60000 ms durations.");
}

int integer(const std::string& text) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("Expected a non-negative whole number.");
    try { return std::stoi(text); }
    catch (const std::out_of_range&) { throw std::invalid_argument("Number is too large."); }
}

const char* name(State state) {
    constexpr const char* labels[] = {"Ready", "Thinking", "Hungry", "Eating", "Finished"};
    return labels[static_cast<int>(state)];
}
const char* name(Status status) {
    constexpr const char* labels[] = {"Idle", "Running", "Paused", "Completed", "Stopped"};
    return labels[static_cast<int>(status)];
}

ResourceManager::ResourceManager(Config config) : config_(config) {
    config_.validate();
    people_.resize(config.count);
    owners_.assign(config.count, -1);
}
double ResourceManager::now() const {
    if (status_ != Status::Running) return accumulated_;
    return accumulated_ + std::chrono::duration<double, std::milli>(Clock::now() - epoch_).count();
}
void ResourceManager::record(double time, int id, const char* action, const std::string& message) {
    history_.push_back({history_.size() + 1, time, id, action, message});
    changed_.notify_all();
}
std::string ResourceManager::forkNames(int id) const {
    return "F" + std::to_string(id) + " and F" + std::to_string((id + 1) % config_.count);
}
void ResourceManager::begin() {
    Lock lock(mutex_);
    if (status_ != Status::Idle) throw std::logic_error("Session already started.");
    epoch_ = Clock::now();
    status_ = Status::Running;
    record(0, -1, "start", "Simulation started");
    for (int i = 0; i < config_.count; ++i) {
        people_[i].state = State::Thinking;
        record(0, i, "think", "Thinking");
    }
}
bool ResourceManager::pause() {
    Lock lock(mutex_);
    if (status_ != Status::Running) return false;
    accumulated_ = now();
    status_ = Status::Paused;
    record(accumulated_, -1, "pause", "Simulation paused");
    return true;
}
bool ResourceManager::resume() {
    Lock lock(mutex_);
    if (status_ != Status::Paused) return false;
    epoch_ = Clock::now();
    status_ = Status::Running;
    record(accumulated_, -1, "resume", "Simulation resumed");
    return true;
}
void ResourceManager::stop() {
    Lock lock(mutex_);
    if (status_ != Status::Running && status_ != Status::Paused) return;
    accumulated_ = now();
    for (auto& p : people_) {
        if (p.state == State::Eating) occupied_ += 2 * (accumulated_ - p.since);
        if (p.state != State::Finished) p.state = State::Ready;
    }
    status_ = Status::Stopped;
    fifo_.clear();
    std::fill(owners_.begin(), owners_.end(), -1);
    eating_ = 0;
    record(accumulated_, -1, "stop", "Stopped and released all forks");
}
// Both condition-variable waits unlock the mutex while the philosopher sleeps.
bool ResourceManager::waitPhase(Lock& lock, double deadline) {
    for (;;) {
        changed_.wait(lock, [&] { return status_ != Status::Paused; });
        if (status_ != Status::Running) return false;
        const double remaining = deadline - now();
        if (remaining <= 0) return true;
        changed_.wait_for(lock, std::chrono::duration<double, std::milli>(remaining),
                          [&] { return status_ != Status::Running; });
    }
}
bool ResourceManager::request(int id) {
    Lock lock(mutex_);
    auto& p = people_[id];
    if (!waitPhase(lock, p.since + config_.thinkMs)) return false;
    p.state = State::Hungry;
    p.since = now();
    fifo_.push_back(id);
    record(p.since, id, "hungry", "Hungry, joined FIFO queue");
    return true;
}
bool ResourceManager::acquire(int id) {
    Lock lock(mutex_);
    const int right = (id + 1) % config_.count;
    changed_.wait(lock, [&] {
        return status_ == Status::Stopped ||
               (status_ == Status::Running && !fifo_.empty() && fifo_.front() == id &&
                owners_[id] == -1 && owners_[right] == -1);
    });
    if (status_ == Status::Stopped) return false;
    const double time = now();
    auto& p = people_[id];
    const double wait = time - p.since;
    p.waitSum += wait;
    p.waitMax = std::max(p.waitMax, wait);
    ++p.grants;
    fifo_.pop_front();
    owners_[id] = owners_[right] = id;
    p.state = State::Eating;
    p.since = time;
    peak_ = std::max(peak_, ++eating_);
    record(time, id, "acquire", "Eating, acquired " + forkNames(id));
    return true;
}
bool ResourceManager::finishMeal(int id) {
    Lock lock(mutex_);
    auto& p = people_[id];
    if (!waitPhase(lock, p.since + config_.eatMs)) return false;
    const double time = now();
    occupied_ += 2 * (time - p.since);
    owners_[id] = owners_[(id + 1) % config_.count] = -1;
    --eating_;
    ++p.meals;
    record(time, id, "release", "Released " + forkNames(id) + ", meal " + std::to_string(p.meals));
    p.since = time;
    if (p.meals == config_.rounds) {
        p.state = State::Finished;
        record(time, id, "finish", "Finished");
        if (++finished_ == config_.count) {
            accumulated_ = time;
            status_ = Status::Completed;
            record(time, -1, "complete", "All philosophers finished");
        }
    } else {
        p.state = State::Thinking;
        record(time, id, "think", "Thinking");
    }
    return true;
}
Snapshot ResourceManager::snapshot(std::size_t after) const {
    Lock lock(mutex_);
    const double elapsed = now();
    double occupied = occupied_;
    for (const auto& p : people_)
        if (p.state == State::Eating) occupied += 2 * (elapsed - p.since);
    const double utilization = elapsed ? 100 * occupied / (config_.count * elapsed) : 0;
    return {config_, status_, elapsed, utilization, eating_, peak_, people_, owners_,
            {fifo_.begin(), fifo_.end()}, {history_.begin() + std::min(after, history_.size()),
            history_.end()}, history_.size()};
}
void ResourceManager::waitForEvent(std::size_t after) const {
    Lock lock(mutex_);
    changed_.wait(lock, [&] { return history_.size() > after ||
                                    status_ == Status::Completed || status_ == Status::Stopped; });
}
