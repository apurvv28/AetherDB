#pragma once

#include <string>
#include <cstdint>
#include <memory>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
#else
    using socket_t = int;
#endif

namespace aether {

class Socket {
public:
    Socket();
    explicit Socket(socket_t fd);
    ~Socket();

    // Prevent copy, allow move
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    bool Connect(const std::string& host, uint16_t port);
    bool Send(const void* buf, size_t len);
    bool Recv(void* buf, size_t len);
    void Close();
    bool IsValid() const;
    socket_t GetFd() const { return fd_; }

private:
    socket_t fd_;
};

class ServerSocket {
public:
    ServerSocket();
    ~ServerSocket();

    ServerSocket(const ServerSocket&) = delete;
    ServerSocket& operator=(const ServerSocket&) = delete;
    ServerSocket(ServerSocket&& other) noexcept;
    ServerSocket& operator=(ServerSocket&& other) noexcept;

    bool Listen(uint16_t port, int backlog = 128);
    Socket Accept();
    void Close();
    bool IsValid() const;

private:
    socket_t fd_;
};

// Helper function to initialize network sub-system (Winsock startup on Windows)
bool InitializeNetwork();
// Helper function to cleanup network sub-system (Winsock cleanup on Windows)
void CleanupNetwork();

} // namespace aether
