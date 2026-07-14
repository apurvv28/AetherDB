#pragma once

#include "storage/disk_manager.hpp"
#include "storage/replacer.hpp"
#include "storage/buffer_pool_manager.hpp"
#include "common/socket.hpp"
#include "common/concurrency.hpp"
#include <memory>
#include <atomic>

namespace aether {

class StorageService {
public:
    StorageService(std::unique_ptr<DiskManager> disk_manager, uint16_t port);
    ~StorageService();

    // Prevent copy and move
    StorageService(const StorageService&) = delete;
    StorageService& operator=(const StorageService&) = delete;

    // Start the server (binds and begins accepting connections in a loop)
    void Start();

    // Stop the server and close all connections
    void Stop();

private:
    // Handle an individual client connection
    void HandleClient(Socket client_socket);

    std::unique_ptr<DiskManager> disk_manager_;
    std::unique_ptr<Replacer> replacer_;
    std::unique_ptr<BufferPoolManager> buffer_pool_manager_;
    uint16_t port_;
    ServerSocket server_socket_;
    std::atomic<bool> is_running_;
};

} // namespace aether
