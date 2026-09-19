#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <atomic>
#include <chrono>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")


struct Backend {
    std::string ip;
    int port;
    std::atomic<bool> is_alive{true};

    Backend(std::string ip, int port, bool alive = true) : ip(std::move(ip)), port(port), is_alive(alive) {}

    Backend(const Backend& other) : ip(other.ip), port(other.port), is_alive(other.is_alive.load()) {}

    Backend& operator=(const Backend &other) {
        if (this != &other) {
            ip = other.ip;
            port = other.port;
            is_alive.store(other.is_alive.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        return *this;
    }
};

std::vector<Backend> BACKEND_POOL = {
    {"127.0.0.1", 9001},
    {"127.0.0.1", 9002},
    {"127.0.0.1", 9003},
};

// Bien dem client nguyen tu (std::atomic)
std::atomic<size_t> g_rr_counter(0);

// Ham lay nhung server is_alive = true
int get_next_backend_index() {
    size_t pool_size = BACKEND_POOL.size();
    for (size_t i = 0; i < pool_size; ++i) {
        size_t index = g_rr_counter.fetch_add(1) % pool_size;
        if (BACKEND_POOL[index].is_alive.load()) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void health_check() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5)); // Kiem tra server moi 5 giay
        std::cout << "[Health check] Thuc hien kiem tra trang thai Backend Pool...\n";
        for (auto& backend: BACKEND_POOL) {
            SOCKET test_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (test_socket == INVALID_SOCKET) continue;
            struct sockaddr_in test_sock{};
            test_sock.sin_family = AF_INET;
            test_sock.sin_port = htons(backend.port);
            inet_pton(AF_INET, backend.ip.c_str(), &test_sock.sin_addr);

            int res = connect(test_socket, (SOCKADDR*)&test_sock, sizeof(test_sock));
            bool current_status = (res != SOCKET_ERROR);
            if (backend.is_alive.load() != current_status) {
                backend.is_alive.store(current_status);
                if (current_status) {
                    std::cout << "  [+] Server " << backend.port << " DA PHUC HOI (ONLINE)\n";
                } else {
                    std::cout << "  [-] Server " << backend.port << " DA SAP (OFFLINE)\n";
                }
            }
            closesocket(test_socket);
        }
    }
}