#include "storage/disk_manager.hpp"
#include "storage/storage_service.hpp"
#include "common/logger.hpp"
#include <iostream>
#include <memory>
#include <csignal>
#include <atomic>
#include <string>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    inline void CrossPlatformSleep(int ms) { Sleep(ms); }
#else
    #include <unistd.h>
    inline void CrossPlatformSleep(int ms) { usleep(ms * 1000); }
#endif

std::atomic<bool> shutdown_requested(false);

void SignalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        shutdown_requested = true;
    }
}

int main(int argc, char* argv[]) {
    // Initialize logging
    aether::InitializeLogger();
    
    // Default arguments
    uint16_t port = 9001;
    std::string db_file = "aether.db";
    
    // Simple manual argument parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if ((arg == "--db" || arg == "-d") && i + 1 < argc) {
            db_file = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "AetherStorage Service Daemon\n"
                      << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  -p, --port <port>   Port to listen on (default: 9001)\n"
                      << "  -d, --db <file>     Database file path (default: aether.db)\n"
                      << "  -h, --help          Show this help message\n";
            return 0;
        }
    }
    
    // Register signal handlers for clean shutdown
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    
    try {
        spdlog::info("Initializing AetherStorage with database file: {}", db_file);
        auto disk_manager = std::make_unique<aether::DiskManager>(db_file);
        
        spdlog::info("Starting AetherStorage Service on port: {}", port);
        aether::StorageService service(std::move(disk_manager), port);
        service.Start();
        
        // Wait for shutdown signal (Ctrl+C)
        while (!shutdown_requested) {
            CrossPlatformSleep(100);
        }
        
        spdlog::info("Shutdown signal received. Stopping AetherStorage Service...");
        service.Stop();
        spdlog::info("AetherStorage Service exited cleanly.");
        
    } catch (const std::exception& e) {
        spdlog::critical("Fatal error in AetherStorage Daemon: {}", e.what());
        return 1;
    }
    
    return 0;
}
