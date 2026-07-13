#include "common/socket.hpp"
#include <iostream>
#include <cstring>

#ifdef _WIN32
    // Winsock links already handled by CMake
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    
    #ifndef INVALID_SOCKET
        #define INVALID_SOCKET (-1)
    #endif
    #ifndef SOCKET_ERROR
        #define SOCKET_ERROR (-1)
    #endif
#endif

namespace aether {

// Network Initialization Helper
bool InitializeNetwork() {
#ifdef _WIN32
    WSADATA wsaData;
    int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (res != 0) {
        std::cerr << "WSAStartup failed: " << res << std::endl;
        return false;
    }
#endif
    return true;
}

void CleanupNetwork() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// Socket Implementation
Socket::Socket() : fd_(INVALID_SOCKET) {}

Socket::Socket(socket_t fd) : fd_(fd) {}

Socket::~Socket() {
    Close();
}

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) {
    other.fd_ = INVALID_SOCKET;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        Close();
        fd_ = other.fd_;
        other.fd_ = INVALID_SOCKET;
    }
    return *this;
}

bool Socket::Connect(const std::string& host, uint16_t port) {
    Close();
    
    struct addrinfo hints, *res = nullptr;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0) {
        return false;
    }
    
    fd_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd_ == INVALID_SOCKET) {
        freeaddrinfo(res);
        return false;
    }
    
    if (connect(fd_, res->ai_addr, static_cast<int>(res->ai_addrlen)) == SOCKET_ERROR) {
        Close();
        freeaddrinfo(res);
        return false;
    }
    
    freeaddrinfo(res);
    return true;
}

bool Socket::Send(const void* buf, size_t len) {
    if (!IsValid()) return false;
    const char* ptr = reinterpret_cast<const char*>(buf);
    size_t sent = 0;
    while (sent < len) {
        int bytes = send(fd_, ptr + sent, static_cast<int>(len - sent), 0);
        if (bytes <= 0) {
            return false;
        }
        sent += bytes;
    }
    return true;
}

bool Socket::Recv(void* buf, size_t len) {
    if (!IsValid()) return false;
    char* ptr = reinterpret_cast<char*>(buf);
    size_t received = 0;
    while (received < len) {
        int bytes = recv(fd_, ptr + received, static_cast<int>(len - received), 0);
        if (bytes <= 0) {
            return false;
        }
        received += bytes;
    }
    return true;
}

void Socket::Close() {
    if (IsValid()) {
#ifdef _WIN32
        closesocket(fd_);
#else
        close(fd_);
#endif
        fd_ = INVALID_SOCKET;
    }
}

bool Socket::IsValid() const {
    return fd_ != INVALID_SOCKET;
}

// ServerSocket Implementation
ServerSocket::ServerSocket() : fd_(INVALID_SOCKET) {}

ServerSocket::~ServerSocket() {
    Close();
}

ServerSocket::ServerSocket(ServerSocket&& other) noexcept : fd_(other.fd_) {
    other.fd_ = INVALID_SOCKET;
}

ServerSocket& ServerSocket::operator=(ServerSocket&& other) noexcept {
    if (this != &other) {
        Close();
        fd_ = other.fd_;
        other.fd_ = INVALID_SOCKET;
    }
    return *this;
}

bool ServerSocket::Listen(uint16_t port, int backlog) {
    Close();
    
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ == INVALID_SOCKET) {
        return false;
    }
    
    // Allow address reuse
    int optval = 1;
#ifdef _WIN32
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&optval), sizeof(optval));
#else
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#endif

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        Close();
        return false;
    }
    
    if (listen(fd_, backlog) == SOCKET_ERROR) {
        Close();
        return false;
    }
    
    return true;
}

Socket ServerSocket::Accept() {
    if (!IsValid()) return Socket(INVALID_SOCKET);
    
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    socket_t client_fd = accept(fd_, reinterpret_cast<struct sockaddr*>(&addr), &addr_len);
    if (client_fd == INVALID_SOCKET) {
        return Socket(INVALID_SOCKET);
    }
    return Socket(client_fd);
}

void ServerSocket::Close() {
    if (IsValid()) {
#ifdef _WIN32
        closesocket(fd_);
#else
        close(fd_);
#endif
        fd_ = INVALID_SOCKET;
    }
}

bool ServerSocket::IsValid() const {
    return fd_ != INVALID_SOCKET;
}

} // namespace aether
