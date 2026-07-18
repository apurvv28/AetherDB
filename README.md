# AetherDB — Microservice-Based Relational Database Engine

AetherDB is a relational database management system (RDBMS) built from first principles and structured in a decoupled **microservice architecture**. Instead of grouping the compute engine, storage layout, logging, and concurrency controls into a single process, AetherDB splits them into independent, modular services communicating over TCP. 

This decoupling mimics modern cloud-native architectures (like Amazon Aurora and TiDB), separating compute from storage, making database internals transparent, modular, and highly educational.

---

## 1. Project Overview & Stakeholders

### What is Being Built?
A single-node relational database engine decomposed into four core TCP services:
1. **AetherStorage Service** (Port 9001): Page-level storage substrate with an in-memory buffer pool cache.
2. **AetherLog Service** (Port 9002): Write-ahead logging (WAL) and crash-recovery daemon using ARIES.
3. **AetherConcurrency Service** (Port 9003): Transaction state machine and row-level 2PL lock table.
4. **AetherQuery Service** (Port 9000): Lexer, parser, planner, and Volcano iterator execution engine.

### Who are the Stakeholders?
* **Systems Engineering Learners / Students**: Who want to look inside the black box of database engines (understanding exactly how records are laid out on disk, how transaction logs guarantee durability, and how network RPCs operate at the system level).
* **Technical Interviewers & Recruiters**: Seeking evidence of strong C++ engineering capability, systems designs (TCP, threading, sockets, serialization), and fundamental computer science depth.
* **Database Practitioners**: Interested in educational cloud-native database layouts built without external ORMs or engine layers.

---

## 2. Phased Implementation Roadmap

Below is the status of each implementation stage of AetherDB:

* [x] **Phase 1 — Disk Manager & Page Layout**
  * *Status*: **Completed**
  * *Description*: Implemented the raw database file coordinator (`DiskManager`) slicing the storage file into fixed-size 4KB pages. Managed deleted pages via a disk-linked free-list stack to prevent file bloat. Bound the storage engine to a multi-threaded TCP server daemon (`AetherStorage`) speaking a custom binary request/response wire protocol.
* [x] **Phase 2 — Buffer Pool Manager**
  * *Description*: Add an in-memory page frame cache (buffer pool) sitting on top of the storage engine. Implement Clock or LRU page eviction to minimize disk read/write cycles.
* [x] **Phase 3 — B+ Tree Index**
  * *Description*: Implement a disk-backed B+ tree index spanning internal routing pages and leaf data pages, enabling fast $O(\log N)$ point and range lookups.
* [x] **Phase 4 — Slotted-Page Record Storage & Catalog**
  * *Description*: Implement a slotted-page architecture to store variable-length records (VARCHARs, etc.) inside the 4KB pages, and create the schema catalog tracking tables and columns.
* [ ] **Phase 5 — Write-Ahead Log (WAL) & Crash Recovery**
  * *Description*: Build the log service daemon appending log records to disk, enforcing the Write-Ahead Logging rule, and running ARIES-style analysis, redo, and undo recovery passes after crashes.
* [ ] **Phase 6 — Transactions & Concurrency Control**
  * *Description*: Build the concurrency daemon providing Shared/Exclusive locks at row granularity, implementing Strict Two-Phase Locking (2PL), and performing wait-for graph cycle analysis for deadlock detection.
* [ ] **Phase 7 — SQL Parser & Query Executor**
  * *Description*: Hand-write a SQL lexer, recursive-descent parser, planner (mapping queries to sequential vs. index scans), and a Volcano-style iterator model executor.
* [ ] **Phase 8 — Client Wire Protocol & Benchmarking**
  * *Description*: Expose a client CLI (`aether-cli`) to run queries over the wire and run load generators comparing AetherDB throughput/latency directly against SQLite.

---

## 3. Environment & Compilation Setup

AetherDB is written in standard C++14 to compile seamlessly on legacy and modern compiler toolchains (such as GCC 6.3/MinGW). It contains **zero external dependencies** for logging and unit testing, optimizing compilation times and footprint.

### Build Instructions
Ensure you have CMake and a C++ compiler installed. Then run:
```bash
# 1. Generate build makefiles
cmake -G "MinGW Makefiles" -B build

# 2. Compile all targets
cmake --build build
```

### Running Unit Tests
Execute the compiled test binary to verify the `DiskManager` page allocation, free list reuse, and disk persistence logic:
```bash
.\build\disk_manager_test.exe
```

### Starting the Storage Daemon
Run the storage service listening on port 9001 (by default):
```bash
.\build\aether-storage-service.exe --db aether.db --port 9001
```

---

## 4. Architectural Deep-Dives
For detailed specifications on designs and APIs, refer to:
* **System Architecture & Mermaid Charts**: [system_architecture.md](file:///d:/VIT/AetherDB/system_design/system_architecture.md)
* **Phase 1 Storage Substrate Deep-Dive**: [Phase1_DiskManager.md](file:///d:/VIT/AetherDB/docs/Phase1_DiskManager.md)
