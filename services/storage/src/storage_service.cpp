#include "storage/storage_service.hpp"
#include "common/protocol.hpp"
#include "common/logger.hpp"
#include <cstring>
#include <exception>

namespace aether {

StorageService::StorageService(std::unique_ptr<DiskManager> disk_manager, uint16_t port)
    : disk_manager_(std::move(disk_manager)), port_(port), is_running_(false) {}

StorageService::~StorageService() {
    Stop();
}

void StorageService::Start() {
    if (is_running_) return;

    if (!InitializeNetwork()) {
        throw std::runtime_error("Failed to initialize network system");
    }

    if (!server_socket_.Listen(port_)) {
        CleanupNetwork();
        throw std::runtime_error("StorageService failed to listen on port " + std::to_string(port_));
    }

    is_running_ = true;
    spdlog::info("StorageService daemon started, listening on port {}...", port_);

    // Spawn thread to accept connections
    Thread acceptor_thread([this]() {
        while (is_running_) {
            Socket client_socket = server_socket_.Accept();
            if (!is_running_) {
                break;
            }
            if (client_socket.IsValid()) {
                spdlog::debug("New connection accepted.");
                auto sock_ptr = std::make_shared<Socket>(std::move(client_socket));
                Thread client_thread([this, sock_ptr]() {
                    this->HandleClient(std::move(*sock_ptr));
                });
                client_thread.Detach(); // Detach to clean up automatically
            }
        }
    });
    
    // We detach the acceptor thread as it will run for the lifetime of the service
    acceptor_thread.Detach();
}

void StorageService::Stop() {
    if (!is_running_) return;

    is_running_ = false;
    server_socket_.Close();
    
    if (disk_manager_) {
        disk_manager_->Close();
    }
    
    CleanupNetwork();
    spdlog::info("StorageService daemon stopped.");
}

void StorageService::HandleClient(Socket client_socket) {
    try {
        while (is_running_) {
            protocol::RequestHeader header;
            if (!client_socket.Recv(&header, sizeof(header))) {
                spdlog::debug("Client disconnected or connection closed.");
                break;
            }

            if (header.op_type == static_cast<uint8_t>(protocol::OpType::READ_PAGE)) {
                page_id_t page_id = header.page_id;
                char buffer[PAGE_SIZE];
                protocol::ResponseHeader resp;
                resp.page_id = page_id;

                try {
                    disk_manager_->ReadPage(page_id, buffer);
                    resp.status = static_cast<uint8_t>(protocol::StatusCode::SUCCESS);
                    resp.data_len = PAGE_SIZE;

                    if (client_socket.Send(&resp, sizeof(resp)) && 
                        client_socket.Send(buffer, PAGE_SIZE)) {
                        spdlog::debug("Served READ_PAGE for page_id {}", page_id);
                    } else {
                        spdlog::error("Failed to send READ_PAGE response to client");
                        break;
                    }
                } catch (const std::exception& e) {
                    spdlog::error("Error serving READ_PAGE for page_id {}: {}", page_id, e.what());
                    resp.status = static_cast<uint8_t>(protocol::StatusCode::ERROR);
                    resp.data_len = 0;
                    client_socket.Send(&resp, sizeof(resp));
                }
            }
            else if (header.op_type == static_cast<uint8_t>(protocol::OpType::WRITE_PAGE)) {
                page_id_t page_id = header.page_id;
                char buffer[PAGE_SIZE];
                protocol::ResponseHeader resp;
                resp.page_id = page_id;
                resp.data_len = 0;

                if (!client_socket.Recv(buffer, PAGE_SIZE)) {
                    spdlog::error("Failed to read WRITE_PAGE payload for page_id {}", page_id);
                    break;
                }

                try {
                    disk_manager_->WritePage(page_id, buffer);
                    resp.status = static_cast<uint8_t>(protocol::StatusCode::SUCCESS);
                    if (client_socket.Send(&resp, sizeof(resp))) {
                        spdlog::debug("Served WRITE_PAGE for page_id {}", page_id);
                    } else {
                        break;
                    }
                } catch (const std::exception& e) {
                    spdlog::error("Error serving WRITE_PAGE for page_id {}: {}", page_id, e.what());
                    resp.status = static_cast<uint8_t>(protocol::StatusCode::ERROR);
                    client_socket.Send(&resp, sizeof(resp));
                }
            }
            else if (header.op_type == static_cast<uint8_t>(protocol::OpType::ALLOCATE_PAGE)) {
                protocol::ResponseHeader resp;
                resp.data_len = 0;

                try {
                    page_id_t new_page_id = disk_manager_->AllocatePage();
                    resp.status = static_cast<uint8_t>(protocol::StatusCode::SUCCESS);
                    resp.page_id = new_page_id;
                    if (client_socket.Send(&resp, sizeof(resp))) {
                        spdlog::debug("Served ALLOCATE_PAGE, returned page_id {}", new_page_id);
                    } else {
                        break;
                    }
                } catch (const std::exception& e) {
                    spdlog::error("Error serving ALLOCATE_PAGE: {}", e.what());
                    resp.status = static_cast<uint8_t>(protocol::StatusCode::ERROR);
                    resp.page_id = INVALID_PAGE_ID;
                    client_socket.Send(&resp, sizeof(resp));
                }
            }
            else if (header.op_type == static_cast<uint8_t>(protocol::OpType::DEALLOCATE_PAGE)) {
                page_id_t page_id = header.page_id;
                protocol::ResponseHeader resp;
                resp.page_id = page_id;
                resp.data_len = 0;

                try {
                    disk_manager_->DeallocatePage(page_id);
                    resp.status = static_cast<uint8_t>(protocol::StatusCode::SUCCESS);
                    if (client_socket.Send(&resp, sizeof(resp))) {
                        spdlog::debug("Served DEALLOCATE_PAGE for page_id {}", page_id);
                    } else {
                        break;
                    }
                } catch (const std::exception& e) {
                    spdlog::error("Error serving DEALLOCATE_PAGE for page_id {}: {}", page_id, e.what());
                    resp.status = static_cast<uint8_t>(protocol::StatusCode::ERROR);
                    client_socket.Send(&resp, sizeof(resp));
                }
            }
            else {
                spdlog::warn("Received unknown operation code: {}", header.op_type);
                protocol::ResponseHeader resp;
                resp.status = static_cast<uint8_t>(protocol::StatusCode::ERROR);
                resp.page_id = INVALID_PAGE_ID;
                resp.data_len = 0;
                client_socket.Send(&resp, sizeof(resp));
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Exception in client handler thread: {}", e.what());
    }
    client_socket.Close();
}

} // namespace aether
