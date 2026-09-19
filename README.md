# C++ Layer 4 TCP Load Balancer & Reverse Proxy

Một hệ thống Load Balancer và Reverse Proxy hoạt động ở Tầng 4 (Transport Layer - TCP) được viết hoàn toàn bằng C++ thuần túy trên nền tảng Windows (sử dụng Winsock2 API) mà không phụ thuộc vào các thư viện mạng bên thứ ba phức tạp. 

Project này được xây dựng nhằm phục vụ mục đích học tập của mình

---

## Các tính năng nổi bật

1. **TCP Stream Forwarding (Bidirectional Bridge):** 
   * Đảm nhận việc tiếp nhận kết nối từ Client và chuyển tiếp luồng byte hai chiều (Client $\leftrightarrow$ Backend) một cách độc lập thông qua các luồng riêng biệt (`std::thread`).
   * Sử dụng cơ chế gửi vét cạn (`while` loop) và đóng nửa kênh (`shutdown(..., SD_SEND)`) giúp đảm bảo toàn vẹn dữ liệu, tránh mất gói tin.

2. **Round Robin Load Balancing:**
   * Quản lý một danh sách các máy chủ backend (Backend Pool).
   * Phân phối lưu lượng tuần tự và công bằng giữa các server bằng biến đếm nguyên tử an toàn đa luồng (`std::atomic`), giải quyết triệt để bài toán xung đột dữ liệu (Race Condition).

3. **Active Health Check (Kiểm tra sức khỏe chủ động):**
   * Chạy một luồng ngầm (Background Worker) định kỳ thăm dò trạng thái sống/chết của các backend server.
   * Tự động cô lập các server bị sập (Offline) và khôi phục lại (Online) ngay khi chúng hoạt động trở lại, đảm bảo hệ thống có tính sẵn sàng cao.

4. **IP-based Rate Limiter (Thuật toán Token Bucket):**
   * Bảo vệ hệ thống khỏi các hành vi spam request hoặc tấn công từ chối dịch vụ cơ bản (DoS).
   * Quản lý hạn mức theo từng địa chỉ IP của Client với cơ chế "Thùng thẻ" tự động nạp bù theo thời gian thực (`Refill Rate`), phản hồi mã lỗi `429 Too Many Requests` khi vượt ngưỡng.

---

## Yêu cầu hệ thống (Prerequisites)

* **Hệ điều hành:** Windows 10 / 11.
* **Trình biên dịch C++:** Hỗ trợ chuẩn C++17 trở lên (Ví dụ: MinGW-w64 `g++` hoặc MSVC trên Visual Studio).
* **Công cụ hỗ trợ test:** Python (để dựng nhanh HTTP server giả lập) và công cụ dòng lệnh `curl`.

---

## Hướng dẫn Biên dịch và Chạy (Build & Run)

### 1. Biên dịch mã nguồn
Mở PowerShell hoặc Command Prompt tại thư mục chứa mã nguồn và chạy lệnh biên dịch (đối với MinGW):
```powershell
g++ -std=c++17 load_balancer_health.cpp -o load_balancer.exe -lws2_32