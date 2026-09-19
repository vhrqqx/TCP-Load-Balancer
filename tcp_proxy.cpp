#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <vector>
#include "load_balancer.h"
#include "rate_limiter.h"

#pragma comment(lib, "ws2_32.lib")

constexpr int LISTEN_PORT = 8080;
constexpr size_t BUFFER_SIZE = 4096;

void transfer_stream(SOCKET source_sock, SOCKET destination_sock) {
    std::vector<char> buffer(BUFFER_SIZE);
    int bytes_read;
    while ((bytes_read = recv(source_sock, buffer.data(), (int)buffer.size(), 0)) > 0) {
        int total_bytes_sent = 0;
        while (total_bytes_sent < bytes_read) {
            int bytes_sent = send(destination_sock, buffer.data() + total_bytes_sent, bytes_read - total_bytes_sent, 0);
            if (bytes_sent == SOCKET_ERROR) {
                goto cleanup;
            }
            total_bytes_sent += bytes_sent;
        }
    }
    cleanup:
        shutdown(destination_sock, SD_SEND);
}

void handle_client(SOCKET client_sock) {    
    // Tao moi socket moi ket noi toi backend server
    SOCKET backend_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int backend_target = get_next_backend_index();
    if (backend_target == -1) {
        std::cerr << "[-] 503: SERVICE UNAVAILABLE: Khong co backend nao kha dung";
    }
    Backend& target = BACKEND_POOL[backend_target]; 
    std::cout << "[*] Điều phối request tới Backend: " << target.ip << ":" << target.port << "\n";
    if (backend_sock == INVALID_SOCKET) {
        std::cout << "[-] Failed while trying to create a socket for backend server.\n" << WSAGetLastError() << "\n";
        closesocket(client_sock);
        return;
    }
    struct sockaddr_in backend_addr{};
    backend_addr.sin_family = AF_INET;
    backend_addr.sin_port = htons(target.port);
    inet_pton(AF_INET, target.ip.c_str(), &backend_addr.sin_addr);

    // Ket noi toi backend
    if (connect(backend_sock, (SOCKADDR*)&backend_addr, sizeof(backend_addr)) == SOCKET_ERROR) {
        std::cout << "[-] Error while trying to connect to the backend server.\n" << WSAGetLastError() << "\n";
        target.is_alive.store(false);
        closesocket(client_sock);
        closesocket(backend_sock);
        return;
    }
    std::cout << "[+] Connected to backend server!\n";
    // Tao 2 thread doc lap chuyen tiep du lieu
    std::thread client_to_backend(transfer_stream, client_sock, backend_sock);
    std::thread backend_to_client(transfer_stream, backend_sock, client_sock);
    if (client_to_backend.joinable()) client_to_backend.join();
    if (backend_to_client.joinable()) backend_to_client.join();

    closesocket(client_sock);
    closesocket(backend_sock);
    std::cout << "Phien ket noi ket thuc.\n";
}

int main() {
    WSADATA wsa;
    SOCKET proxy_socket = INVALID_SOCKET;
    int startup = WSAStartup(MAKEWORD(2,2), &wsa);
    if (startup != 0) {
        std::cout << "[-] Startup failed!\n" << WSAGetLastError() << "\n";
    }
    std::cout << "[+] Startup successful!\n";

    std::thread healthThread(health_check);
    healthThread.detach();

    //Tao socket lang nghe cho proxy
    proxy_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (proxy_socket == INVALID_SOCKET) {
        std::cout << "[-] Failed while trying to create a socket for proxy.\n" << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }
    std::cout << "[+] Proxy socket created successfully!\n";
    // Tai su dung port
    BOOL opt = TRUE;
    setsockopt(proxy_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    // Bind va listen socket
    struct sockaddr_in proxy_sock;
    memset(&proxy_sock, 0, sizeof(proxy_sock));
    proxy_sock.sin_family = AF_INET;
    proxy_sock.sin_port = htons(LISTEN_PORT);
    proxy_sock.sin_addr.s_addr = INADDR_ANY;
    if (bind(proxy_socket, (SOCKADDR*)&proxy_sock, sizeof(proxy_sock)) == SOCKET_ERROR) {
        std::cout << "[-] Binding failed.\n" << WSAGetLastError() << "\n";
        closesocket(proxy_socket);
        WSACleanup();
        return 1;
    }
    std::cout << "[+] Binding success!\n";
    if (listen(proxy_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cout << "[-] Listen failed.\n" << WSAGetLastError() << "\n";
        closesocket(proxy_socket);
        WSACleanup();
        return 1;
    }
    std::cout << "[+] Heard a connection!\n";
    std::cout << "[+] LOAD BALANCER DANG CHAY TAI PORT: " << LISTEN_PORT << "/\n";
    std::cout << "[+] Danh sach cac Backend pool: " << "\n";
    for (const auto& b: BACKEND_POOL) {
        std::cout << "    - " << b.ip << ":" << b.port << "\n";
    }
    // Vong lap while de tiep nhan ket noi
    while (true){
        struct sockaddr_in client_addr{};
        memset(&client_addr, 0, sizeof(client_addr));
        int client_len = sizeof(client_addr);
        SOCKET client_socket = accept(proxy_socket, (SOCKADDR*)&client_addr, &client_len);
        if (client_socket == INVALID_SOCKET) {
            std::cout << "[-] Error while trying to accept connection.\n" << WSAGetLastError() << "\n";
            std::thread session(handle_client, client_socket);
            session.detach();
            closesocket(proxy_socket);
            continue;
        }
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "\n[+] New client connecting from " << client_ip << ntohs(client_addr.sin_port) << "\n";
        if (!check_limit_rate(client_ip)) {
            std::cout << "[-] Bi chan do spam request (Rate Limited) tu IP: " << client_ip << "\n";
            
            // Phản hồi nhanh mã lỗi HTTP 429 cho client biết họ bị giới hạn tốc độ
            const char* http_429 = "HTTP/1.1 429 Too Many Requests\r\nContent-Length: 21\r\n\r\nToo Many Requests\r\n";
            send(client_socket, http_429, static_cast<int>(strlen(http_429)), 0);
            
            // Đóng socket ngay lập tức và bỏ qua việc đẩy sang backend
            closesocket(client_socket);
            continue;
        }
        std::thread session(handle_client, client_socket);
        session.detach();
    }
    closesocket(proxy_socket);
    //WSACleanup();
    return 0;
}