# Thread Time-Sharing Monitor

A visual C application that demonstrates **multithreading and CPU time-sharing** using POSIX threads (`pthread`) and SDL2.

The application creates multiple counter threads and provides a graphical dashboard showing which thread is currently using the CPU, which threads are waiting, how often context switches occur, and how CPU time is distributed over time.

> **Note:** This is an educational visualization of time-sharing. It does not implement or replace the Linux kernel scheduler.

---

## Features

* 🧵 Multiple POSIX threads using `pthread`
* 🔢 Independent counter for each thread
* 🖥️ Visual CPU core representation
* 🟢 Live `RUNNING` / `WAITING` / `STOPPED` states
* ⏱️ Visual time-slice progress
* 📊 Per-thread execution statistics
* 🔄 Context-switch counter
* 📈 Animated CPU activity bars
* 📜 Scrolling CPU execution timeline
* 📋 Visual ready queue
* ⏸️ Pause and resume simulation
* 🔄 Reset statistics
* 🎨 SDL2 graphical interface
* 🔤 SDL2_ttf text rendering
* 🧠 CPU affinity support on Linux

---

## What Does This Demonstrate?

The main purpose of this project is to make **thread scheduling and time-sharing easier to understand visually**.

Imagine six threads competing for one CPU core:

```text
                    CPU
                     │
          ┌──────────┼──────────┐
          │          │          │
          ▼          ▼          ▼
       Thread 0   Thread 1   Thread 2
          │          │          │
          └──────────┼──────────┘
                     │
                Time Sharing
```

Only one of the demonstration threads is shown as owning the CPU at a time.

The application continuously switches between them:

```text
TIME →

T0  █████
T1       █████
T2            █████
T3                 █████
T4                      █████
T5                           █████
```

This makes the concept of CPU time-sharing easier to see than simply printing thread messages in a terminal.

---

## Dashboard

The application is divided into several sections.

### CPU Core

The CPU panel shows the thread currently executing:

```text
┌─────────────────────────────┐
│ CPU CORE                    │
│                             │
│          ┌─────────┐        │
│          │   CPU   │        │
│          │ THREAD 3│        │
│          │ EXECUTE │        │
│          └─────────┘        │
│                             │
│ Time slice                  │
│ █████████████░░░░  72%      │
└─────────────────────────────┘
```

The time-slice indicator shows how far the current demonstration slice has progressed.

---

### Thread Cards

Each thread has its own card:

```text
┌──────────────────────────┐
│ THREAD 3                 │
│ ● RUNNING                │
│                          │
│ COUNTER        182391    │
│ Switches:          32    │
│ CPU time:       3200ms   │
│ █████████████████        │
└──────────────────────────┘
```

Each card displays:

* Thread ID
* Current state
* Counter value
* Number of times selected
* Approximate CPU runtime
* Activity indicator

---

### Thread States

The application uses three visual states:

| State        | Meaning                                         |
| ------------ | ----------------------------------------------- |
| 🟢 `RUNNING` | The thread currently owns the demonstration CPU |
| 🟡 `WAITING` | The thread is waiting for its turn              |
| 🔴 `STOPPED` | The thread has terminated                       |

---

### Ready Queue

The ready queue provides a simplified visualization of threads waiting to execute:

```text
READY QUEUE

[T0] [T1] [T2] [T4] [T5]
```

The currently executing thread is removed from the waiting queue visually.

---

### CPU Timeline

The timeline shows the execution history:

```text
CPU TIMELINE

T0  ████
T1      ████
T2          ████
T3              ████
T4                  ████
T5                      ████

PAST                                      NOW
```

This is one of the most useful parts of the application because it makes CPU time-sharing visible as a sequence of execution periods.

---

# Architecture

The program consists of:

```text
                         ┌──────────────────────┐
                         │       SDL2 GUI       │
                         │                      │
                         │  CPU / Threads /     │
                         │  Queue / Timeline    │
                         └──────────┬───────────┘
                                    │
                              Shared State
                                    │
                              pthread_mutex
                                    │
             ┌──────────────────────┼──────────────────────┐
             │          │            │          │           │
             ▼          ▼            ▼          ▼           ▼
          Thread 0   Thread 1     Thread 2   Thread 3    Thread ...
             │          │            │          │
             └──────────┴────────────┴──────────┘
                            │
                       CPU 0 affinity
```

Each counter is represented by a separate POSIX thread.

The threads share information with the GUI through shared memory protected by a mutex.

---

# How the Threads Work

Each counter thread repeatedly performs a small amount of CPU work.

Simplified:

```c
while (running) {

    select_thread(thread_id);

    work_for_time_slice();

    mark_thread_waiting();

    sched_yield();
}
```

The thread:

1. Announces itself as the currently executing thread.
2. Performs CPU-intensive work.
3. Updates its counter.
4. Records its runtime.
5. Changes its state to `WAITING`.
6. Calls `sched_yield()`.
7. Another thread gets an opportunity to execute.

---

# CPU Affinity

The application attempts to pin the worker threads to **CPU 0** on Linux.

```c
CPU_ZERO(&cpuset);
CPU_SET(0, &cpuset);

pthread_setaffinity_np(
    pthread_self(),
    sizeof(cpuset),
    &cpuset
);
```

This is done because a modern computer may have multiple CPU cores.

For example, with six threads and eight CPU cores, several threads could execute simultaneously.

That would make the demonstration harder to understand.

By placing the threads on one CPU core, the application can visualize the idea of:

```text
Thread 0 ──┐
Thread 1 ──┤
Thread 2 ──┤
Thread 3 ──┼──► CPU 0
Thread 4 ──┤
Thread 5 ──┘
```

---

# Important: This Is Not the Linux Scheduler

This project intentionally simplifies scheduling.

The Linux kernel is still responsible for the actual scheduling of the pthreads.

The application does **not** implement the Linux scheduler.

The project uses:

* CPU affinity
* controlled CPU work periods
* `sched_yield()`
* application-level state tracking

to create an understandable visualization.

Therefore:

```text
Real Linux:

        Linux Kernel Scheduler
                 │
                 ▼
             CPU Core
                 │
          ┌──────┴──────┐
          ▼             ▼
       Thread A      Thread B
```

Whereas this project presents an educational model:

```text
Educational Model:

       Application
            │
            ▼
     ┌─────────────┐
     │ Time Slice  │
     └──────┬──────┘
            │
            ▼
       Thread A
            │
          yield
            │
            ▼
       Thread B
```

The distinction is important when studying operating systems.

---

# Requirements

## Linux

The project is primarily designed for Linux.

Tested concepts include:

* POSIX threads
* Linux CPU affinity
* SDL2
* SDL2_ttf

## Dependencies

You need:

* GCC
* pthread
* SDL2
* SDL2_ttf
* DejaVu Sans or another compatible font

---

# Installation

### Arch Linux

Install the required packages:

```bash
sudo pacman -S gcc sdl2 sdl2_ttf ttf-dejavu
```

---

# Building

Clone the repository:

```bash
git clone https://github.com/Yunis-rgbdev/thread-timesharing-monitor.git
cd thread-timesharing-monitor
```

Compile:

```bash
gcc main.c -o counter_monitor \
    $(pkg-config --cflags --libs sdl2 SDL2_ttf) \
    -pthread
```

Run:

```bash
./counter_monitor
```

---

# Controls

| Key     | Action                        |
| ------- | ----------------------------- |
| `Space` | Pause / Resume                |
| `R`     | Reset counters and statistics |
| `Esc`   | Exit                          |

You can also close the application window to exit.

---

# Project Structure

The basic project structure is:

```text
thread-timesharing-monitor/
│
├── main.c
├── README.md
└── counter_monitor
```

`counter_monitor` is the compiled executable and does not need to be committed to Git.

A `.gitignore` can be used to prevent it from being uploaded:

```gitignore
counter_monitor
*.o
```

---

# Statistics

The application tracks several statistics for each thread:

### Counter

The amount of work performed by the thread.

```text
Counter: 182391
```

### Context Switches

How many times the thread has been selected by the application's demonstration scheduler.

```text
Switches: 32
```

### CPU Runtime

The approximate amount of time the thread spent performing its simulated CPU work.

```text
CPU time: 3200ms
```

### Total Context Switches

The dashboard also tracks how many times execution moved between different threads.

```text
Context switches: 143
```

---

# Synchronization

Multiple threads access shared information simultaneously.

For example:

```text
Worker Thread
     │
     ▼
counter++
     │
     ▼
 shared state
     ▲
     │
     │ mutex
     │
     ▼
 SDL GUI
```

A `pthread_mutex_t` protects shared state.

This prevents data races when the GUI reads information while worker threads are modifying it.

---

# Why Use Threads?

A counter program could easily be written without threads.

For example:

```c
while (running) {
    counter++;
}
```

But that wouldn't demonstrate concurrency.

With multiple threads:

```text
Thread 0 → Counter 0
Thread 1 → Counter 1
Thread 2 → Counter 2
Thread 3 → Counter 3
Thread 4 → Counter 4
Thread 5 → Counter 5
```

we can observe how multiple independent units of execution compete for CPU resources.

---

# Educational Goals

This project can be used to explore:

* Threads
* POSIX `pthread`
* CPU scheduling
* Time-sharing
* Context switching
* CPU affinity
* Mutexes
* Shared memory
* Thread states
* Ready queues
* CPU utilization
* Cooperative yielding
* GUI visualization
* Operating-system scheduling concepts

---

# Limitations

This project intentionally simplifies several aspects of real operating-system scheduling.

It does not provide:

* A real implementation of the Linux scheduler
* True kernel-level context-switch measurements
* Kernel thread scheduling data
* Accurate per-thread CPU utilization
* A real OS ready queue
* Preemptive scheduling implemented by the application
* Multiple physical CPU scheduling visualization

The displayed timeline is an **application-level visualization** based on events recorded by the program.

---

# Possible Future Improvements

Some interesting extensions would be:

* [ ] Add Round-Robin scheduling simulation
* [ ] Add First-Come-First-Serve scheduling
* [ ] Add Priority scheduling
* [ ] Add Shortest Job First
* [ ] Add configurable time slices
* [ ] Add 2, 4, 8 CPU cores
* [ ] Visualize multiple CPUs simultaneously
* [ ] Add thread creation/termination animations
* [ ] Display actual Linux CPU usage
* [ ] Display actual thread IDs
* [ ] Add CPU utilization graphs
* [ ] Add scheduler algorithm selection
* [ ] Add configurable number of threads
* [ ] Add thread priorities
* [ ] Add mutex/lock visualization
* [ ] Visualize blocked threads
* [ ] Visualize context-switch events
* [ ] Add a real scheduler simulation separate from pthread execution

---

# License

This project is intended as an educational project for learning C, POSIX threads, concurrency, and operating-system scheduling concepts.

Add a license here if you decide to distribute the project publicly.
