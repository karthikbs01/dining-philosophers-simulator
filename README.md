# Dining Philosophers

A C++ multithreaded Dining Philosophers simulation with a React web interface, demonstrating deadlock prevention, starvation prevention, and thread synchronization.

Run the CLI to watch a timestamped trace, or use the React dashboard to pause a run and inspect the queue, fork owners and timing statistics. Both interfaces execute the same C++17 worker and resource-manager code.

## Clone and build on Windows

Install Git for Windows, MSYS2, and Node.js 24 with npm. For the browser tests, install Google Chrome or Microsoft Edge. Open the **MSYS2 UCRT64** terminal and install the C++17 compiler, CMake and Ninja.

~~~bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
~~~

In PowerShell, clone the repository and enter its root. If you already have a checkout, open that folder instead.

~~~powershell
git clone https://github.com/karthikbs01/dining-philosophers-simulator.git
Set-Location dining-philosophers-simulator
~~~

Open the repository folder in VS Code and select **Terminal → New Terminal → PowerShell**, or continue in the same shell. Put MSYS2 UCRT64 first on PATH in each new terminal used for builds and CTest. Adjust the prefix if you installed MSYS2 elsewhere.

~~~powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\dining.exe
~~~

**Website.** From the same terminal, build the frontend and start the backend from the project root. Open the address below. The C++ process serves both the built React files and the API. End the server with Ctrl+C.

~~~powershell
Push-Location website\frontend
npm.cmd ci
npm.cmd run build
Pop-Location
.\build\dining_server.exe
~~~

Open [the local simulator](http://127.0.0.1:8080/). Choose the count, rounds and timings and press **Start simulation**. Press **Pause** while someone is eating to see the two owned forks. **Resume** continues the same run. **Reset** cancels the run, releases its resources and returns to idle with the same configuration. After completion, Start launches a fresh run.

**Frontend development.** Keep the C++ server running on port 8080. In a second VS Code PowerShell terminal at the repository root, run the following commands. Vite forwards `/api` to the C++ process and reloads frontend edits automatically.

~~~powershell
Set-Location website\frontend
npm.cmd run dev
~~~

Open [the Vite development interface](http://127.0.0.1:5173/). If 8080 is occupied, reuse the running assignment server or stop it first. For a separate instance serving the built interface, pass a free port, for example `.\build\dining_server.exe 8081`.

### CLI inputs

~~~text
dining [philosophers [rounds [think_ms [eat_ms]]]]
~~~

| Argument | Default | Accepted whole numbers |
| --- | ---: | ---: |
| Philosophers | 5 | 2–10 |
| Rounds per philosopher | 3 | 1–100 |
| Think duration per round | 700 ms | 0–60000 ms |
| Eat duration per round | 450 ms | 0–60000 ms |

Run `.\build\dining.exe 5 3 40 25` for a short demonstration. Zero is a valid phase duration. Invalid input prints usage and returns a nonzero exit code.

The CLI also builds directly without CMake, Node or the HTTP library. In the project-root PowerShell terminal with UCRT64 first on PATH, run the following.

~~~powershell
g++ -std=c++17 -O2 -pthread -static main.cpp philosopher.cpp resource_manager.cpp simulation.cpp -o dining.exe
.\dining.exe 5 3 40 25
~~~

## Assignment and objectives

Problem Statement 6 describes a multithreaded application sharing limited resources. Seat `i` needs both fork `i` and fork `(i + 1) % N` to eat. For five seats, P4 and P0 share F0. The objectives are exclusive fork ownership, progress for every hungry philosopher and concurrent use of disjoint fork pairs.

## Architecture

~~~mermaid
flowchart LR
    CLI["CLI / main.cpp"] --> S["Simulation"]
    UI["React dashboard"] -->|HTTP controls| API["C++ HTTP server"]
    API --> S
    S -->|one thread per seat| W["dine() worker"]
    W --> M["ResourceManager: mutex, condition variable, FIFO"]
    M --> D["Fork owners, event journal, measurements"]
    D -->|snapshot| CLI
    D -->|snapshot| API
    API -->|SSE| UI
~~~

~~~text
dining-philosophers-simulator/
  main.cpp                     CLI arguments, ordered log and summary
  philosopher.h / .cpp         Worker loop
  resource_manager.h / .cpp    Fork monitor, queue, timing and journal
  simulation.h / .cpp          Thread startup, cancellation and joins
  CMakeLists.txt               Engine library, CLI, tests and server targets
  website/
    backend/server.cpp         Session controller, HTTP API, SSE and static files
    backend/vendor/            cpp-httplib header and its MIT license
    frontend/                  Vite project, React components and plain CSS
  tests/
    engine_tests.cpp           Synchronization and statistics checks
    website.test.mjs           Real browser, server and control integration
  README.md
  sample_output.txt            Full trace from an executed CLI run
~~~

The HTTP dependency is bundled at version 0.67.0. Only the backend target includes it. [cpp-httplib upstream](https://github.com/yhirose/cpp-httplib/tree/v0.67.0)

## How one philosopher eats

~~~mermaid
flowchart TD
    A["Think for the configured active time"] --> B["Become hungry and enqueue once"]
    B --> C{"Running, first in FIFO and both forks free?"}
    C -->|No| D["Sleep on the condition variable"]
    D -->|Wake and recheck| C
    C -->|Yes| E["Dequeue, mark both owners, record wait, notify"]
    E --> F["Eat while the resource mutex is available to others"]
    F --> G["Release both forks, count meal and notify"]
    G --> H{"All rounds completed?"}
    H -->|No| A
    H -->|Yes| I["Finish"]
~~~

A **thread** is an independently scheduled path through a process. Each philosopher runs the short loop in `philosopher.cpp`. A **mutex** gives one thread at a time access to the queue, owners, states and journal. The same critical section checks availability and sets both owners, so a second worker cannot act on an outdated observation.

A **condition variable** lets a waiting worker release the mutex and sleep. It reacquires the mutex before checking its predicate again. Granting a request wakes others because the next queue head may already have two free forks. Releasing forks, pausing, resuming and stopping also wake waiters. Timed waits implement thinking and eating and release the mutex during the delay.

**Deadlock freedom.** A hungry worker holds zero forks. An eater owns both and can release both without requesting another fork. This eliminates the cycle in which everyone owns one fork and waits for a neighbor.

**Starvation freedom.** After a philosopher enters the FIFO queue, later requests cannot pass it. Existing eaters finish in finite time, allowing the head to acquire its pair. Applying the same argument to each earlier request eventually brings this philosopher to the head. This assumes finite phase durations, eventual scheduling of runnable threads and eventual resumption of a paused run. FIFO order starts at enqueue time. Thread launch order and seat number do not determine it.

**Throughput trade-off.** Suppose P0 owns F0 and F1. P1 is first in line for F1 and F2, while P3 behind it could use free F3 and F4. Strict FIFO makes P3 wait. If P3 arrives first instead, P0 and P3 can eat together. An eligible-first allocator can improve utilization in the first case, but would need an additional fairness rule to bound overtaking.

### Pause, stop and snapshots

Pause takes the resource mutex, freezes the active clock and changes the session state before waking workers. A timed phase measures its remaining **active** time after resuming. Held forks stay with their eater. A blocked request keeps its FIFO position. Worker transitions check the session under the same mutex.

Reset marks the old run stopped, clears ownership and the queue, and wakes sleeping workers. The controller joins every worker before replacing that run with an idle session. A separate controller mutex orders HTTP commands. Only one run can be active, and a generation number rejects commands targeting an older run.

Each snapshot copies the measured state and its matching event sequence under one lock. JSON serialization, network writes and CLI printing happen after that lock is released.

## Reading the measurements

| Metric | Calculation |
| --- | --- |
| Meals | Increment on releasing both forks after a full eating phase |
| Average wait | Sum of hungry-to-grant intervals divided by number of grants |
| Maximum wait | Longest measured hungry-to-grant interval per seat |
| Total active runtime | Monotonic elapsed time from start to final release, excluding pauses |
| Fork utilization | 100 × total occupied fork-milliseconds / (N × active runtime in ms) |
| Peak eaters | Highest simultaneous eater count observed at a grant |

All times come from `std::chrono::steady_clock`. A grant counts toward waiting statistics immediately. A pending request contributes once granted. A live utilization reading includes partial eating intervals. The UI samples snapshots roughly ten times per second during a run and includes every recorded transition since the previous frame. Very short meals can appear only in the journal between two visual frames.

### Measured example

The saved run used five philosophers, three rounds, 40 ms thinking and 25 ms eating. These lines come from its trace.

~~~text
[44.48 ms] P0 Hungry, joined FIFO queue
[44.50 ms] P0 Eating, acquired F0 and F1
...
Total duration: 345.13 ms
Fork utilization: 57.75%
Maximum simultaneous eaters: 2
~~~

All five finished three meals. P0 averaged 11.04 ms waiting, and P1 averaged 33.72 ms. Their enqueue times and fork conflicts differed. The sample includes the complete per-seat table and every release. [Full CLI output](sample_output.txt)

### Performance observations

Three additional Windows runs used five rounds per seat and 20 ms for both phases.

| Seats | Meals completed | Active runtime | Fork utilization | Peak eaters |
| ---: | ---: | ---: | ---: | ---: |
| 2 | 10 | 360.50 ms | 92.11% | 1 |
| 5 | 25 | 522.89 ms | 63.64% | 2 |
| 10 | 50 | 686.80 ms | 47.53% | 3 |

At two seats, every eater uses every fork. At ten seats the physical ceiling is five simultaneous eaters. This run reached three. Thinking intervals, FIFO head blocking and OS scheduling can all leave forks idle. These are individual measurements. Repeating a command can change arrival order and timings, and OS wake-up latency can extend short phases beyond their requested durations.

## Local HTTP interface

| Method and path | Behavior |
| --- | --- |
| `GET /api/state` | Current configuration, generation, status, seat results, owners, queue, statistics and full journal |
| `GET /api/events` | SSE stream carrying the same state plus newly recorded events |
| `POST /api/start` | Start from idle or completed with `count`, `rounds`, `thinkMs`, `eatMs` and `generation` |
| `POST /api/pause` | Freeze the running session |
| `POST /api/resume` | Continue the paused session |
| `POST /api/reset` | Stop, wake, join and return to idle |

Send URL-encoded form fields. Every POST requires the current `generation` and header `X-Dining-Control: 1`. Success returns 204, invalid fields return 400, and stale or conflicting commands return 409. The controls operate on the shared session across browser tabs.

An SSE connection begins with the current run’s full journal. Further packets carry a generation and a sequence number. React replaces the log after a generation change or reconnection, and appends new sequence numbers within a run. Closing a tab releases its stream and the simulation continues. The server binds to IPv4 loopback, accepts up to four live viewers, and keeps history in memory for the current run. The dashboard displays the newest 40 events initially and can reveal older entries.

## Reproduce the tests

From the project-root PowerShell terminal with the build tools on PATH, run the following. The browser suite starts an isolated backend on a free port and closes it after the test. It uses an installed Chrome. To select Edge, set `$env:BROWSER_CHANNEL = "msedge"` before running it.

~~~powershell
cmake --build build
ctest --test-dir build --output-on-failure
Push-Location website\frontend
npm.cmd ci
npm.cmd run build
npm.cmd test
Pop-Location
~~~

| Executed check | Result on this machine |
| --- | --- |
| Direct C++17 CLI build, default CLI, custom arguments and invalid count | Passed |
| 2, 5 and 10 seats × 3, 5 and 100 rounds | All 9 combinations finished |
| Replay every request, grant and release | FIFO, fork exclusivity, adjacency, meal totals, peak and timing calculations passed |
| Two disjoint grants with controlled arrival order | Both fork pairs held simultaneously |
| Pause during eating, resume, cancel while paused, invalid inputs | Passed |
| Real Chrome → HTTP controls → C++ workers → SSE → React | Passed for reconnect, pause/reload, resume, completion, reset and concurrent Start requests |
| Browser results and complete log compared with backend state | Passed, including fork ownership and per-seat waits |
| Vite production build, development proxy and 360 px viewport overflow check | Passed |

Verified with Windows, MSYS2 UCRT64 GCC 16.2.0, CMake 4.4.5, Ninja 1.13.2, Node 24.22.1 and npm 11.19.1. CTest sets a 90-second deadline for the engine suite to detect a hang.

**ThreadSanitizer.** The Windows GCC link probe failed because the toolchain lacks `libtsan`. A sanitizer result is still unverified. On a Linux machine with GCC or Clang and its ThreadSanitizer runtime, use the following commands for the engine checks.

~~~bash
cmake -S . -B build-tsan -DENABLE_TSAN=ON -DBUILD_WEBSITE=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure
~~~

## Viva questions

**Why is one fork per philosopher sufficient?** A fork sits between each neighboring pair, forming a ring. Each seat can eat only while it owns both adjacent forks.

**Why can adjacent seats never eat together?** Their fork pairs overlap at one index. The owner array has one slot for that index and every grant requires both slots to be free.

**Does one resource mutex serialize all eating?** It serializes changes to ownership. Once the pair is granted, a timed condition-variable wait releases the mutex. A non-adjacent queue head can acquire its own pair during that interval.

**Why recheck after a notification?** Another worker may have changed the state before this worker regained the mutex. Condition variables can also wake spuriously.

**Can fairness guarantee equal waiting times?** FIFO prevents overtaking after enqueue. A head request can encounter busy forks, so individual waits still vary.

**Why use a monotonic clock?** Elapsed waiting and occupancy intervals should keep their order even when the system’s wall clock is adjusted.

**Where would you start a code walkthrough?** Read `dine()`, then trace `request()`, `acquire()` and `finishMeal()` in the resource manager. Use two consecutive grant events before either release to demonstrate simultaneous eating.
