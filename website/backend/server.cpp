#include "simulation.h"
#include "vendor/httplib.h"
#include <atomic>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>

using namespace std::chrono_literals;
using httplib::Request;
using httplib::Response;
constexpr auto noEvents = std::numeric_limits<std::size_t>::max();

struct Packet { int generation; Snapshot state; };

// The controller mutex orders HTTP commands. Workers never acquire it.
class Controller {
public:
    Packet read(int generation = -1, std::size_t after = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        return {generation_, sim_->snapshot(generation == generation_ ? after : 0)};
    }
    void control(const std::string& action, const Request& req, Response& res) {
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            auto number = [&](const char* key) {
                if (req.get_param_value_count(key) != 1)
                    throw std::invalid_argument(std::string("Expected one field named ") + key);
                return integer(req.get_param_value(key));
            };
            if (number("generation") != generation_) throw std::logic_error("Session has changed.");
            const auto current = sim_->snapshot(noEvents);
            if (action == "start") {
                if (current.status == Status::Running || current.status == Status::Paused)
                    throw std::logic_error("A simulation is already active.");
                Config cfg{number("count"), number("rounds"), number("thinkMs"), number("eatMs")};
                auto next = std::make_unique<Simulation>(cfg);
                next->start();
                sim_ = std::move(next);
                ++generation_;
            } else if (action == "reset") {
                auto next = std::make_unique<Simulation>(current.config);
                sim_->stop();
                sim_ = std::move(next);
                ++generation_;
            } else if (action == "pause" ? !sim_->pause() : !sim_->resume()) {
                throw std::logic_error("Command is unavailable in the current state.");
            }
            res.status = 204;
        } catch (const std::invalid_argument& e) {
            res.status = 400;
            res.set_content(e.what(), "text/plain");
        } catch (const std::logic_error& e) {
            res.status = 409;
            res.set_content(e.what(), "text/plain");
        }
    }
private:
    std::mutex mutex_;
    int generation_ = 0;
    std::unique_ptr<Simulation> sim_ = std::make_unique<Simulation>();
};

std::string json(const Packet& packet) {
    const auto& s = packet.state;
    const auto& c = s.config;
    std::ostringstream out;
    out << std::setprecision(12)
        << R"({"generation":)" << packet.generation << R"(,"seq":)" << s.sequence
        << R"(,"status":)" << std::quoted(name(s.status))
        << R"(,"config":{"count":)" << c.count << R"(,"rounds":)" << c.rounds
        << R"(,"thinkMs":)" << c.thinkMs << R"(,"eatMs":)" << c.eatMs << "}"
        << R"(,"elapsed":)" << s.elapsed << R"(,"utilization":)" << s.utilization
        << R"(,"eaters":)" << s.eaters << R"(,"peak":)" << s.peak;
    auto integers = [&](const char* key, const std::vector<int>& values) {
        out << "," << std::quoted(key) << ":[";
        for (std::size_t i = 0; i < values.size(); ++i) out << (i ? "," : "") << values[i];
        out << "]";
    };
    integers("owners", s.owners);
    integers("queue", s.queue);
    out << R"(,"people":[)";
    for (std::size_t i = 0; i < s.people.size(); ++i) {
        const auto& p = s.people[i];
        out << (i ? "," : "") << R"({"state":)" << std::quoted(name(p.state))
            << R"(,"meals":)" << p.meals << R"(,"avgWait":)" << (p.grants ? p.waitSum / p.grants : 0)
            << R"(,"maxWait":)" << p.waitMax << "}";
    }
    out << R"(],"events":[)";
    for (std::size_t i = 0; i < s.events.size(); ++i) {
        const auto& e = s.events[i];
        out << (i ? "," : "") << R"({"seq":)" << e.seq << R"(,"ms":)" << e.ms
            << R"(,"id":)" << e.id << R"(,"action":)" << std::quoted(e.action)
            << R"(,"message":)" << std::quoted(e.message) << "}";
    }
    out << "]}";
    return out.str();
}

volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }

int main(int argc, char** argv) {
    try {
        int port = argc > 1 ? integer(argv[1]) : 8080;
        if (port < 1 || port > 65535 || argc > 2) throw std::invalid_argument("Usage: dining_server [port]");
        Controller controller;
        std::atomic<int> streams{0};
        std::atomic<bool> closing{false};
        httplib::Server server;
        server.new_task_queue = [] { return new httplib::ThreadPool(8, 8, 32); };
        server.set_payload_max_length(4096);
        server.set_write_timeout(3s);
        server.set_mount_point("/", "website/frontend/dist");
        server.Get("/api/state", [&](const Request&, Response& res) {
            res.set_header("Cache-Control", "no-store");
            res.set_content(json(controller.read()), "application/json");
        });
        server.Post(R"(/api/(start|pause|resume|reset))", [&](const Request& req, Response& res) {
            if (req.get_header_value("X-Dining-Control") != "1") {
                res.status = 403;
                return;
            }
            controller.control(req.matches[1].str(), req, res);
        });
        server.Get("/api/events", [&](const Request&, Response& res) {
            if (streams.fetch_add(1) >= 4) {
                --streams;
                res.status = 503;
                res.set_content("Four live viewers are already connected.", "text/plain");
                return;
            }
            res.set_header("Cache-Control", "no-cache, no-transform");
            res.set_chunked_content_provider("text/event-stream",
                [&, generation = -1, seq = std::size_t{0}](std::size_t offset, httplib::DataSink& sink) mutable {
                    if (offset) std::this_thread::sleep_for(100ms);
                    if (closing || !sink.is_writable()) return false;
                    const auto packet = controller.read(generation, seq);
                    // The initial frame on every connection includes this run's full journal.
                    const bool updated = packet.generation != generation || packet.state.sequence != seq ||
                                         packet.state.status == Status::Running;
                    generation = packet.generation;
                    seq = packet.state.sequence;
                    std::string frame = updated ? "data: " + json(packet) + "\n\n" : ": heartbeat\n\n";
                    return sink.write(frame.data(), frame.size());
                }, [&](bool) { --streams; });
        });
        if (!server.bind_to_port("127.0.0.1", port)) throw std::runtime_error("Cannot bind loopback port.");
        std::signal(SIGINT, interrupt);
        std::signal(SIGTERM, interrupt);
        std::cout << "Dining Philosophers server at http://127.0.0.1:" << port << std::endl;
        std::atomic<bool> done{false};
        bool served = false;
        std::thread listener([&] { served = server.listen_after_bind(); done = true; });
        while (!interrupted && !done) std::this_thread::sleep_for(100ms);
        closing = true;
        server.stop();
        listener.join();
        return served ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
