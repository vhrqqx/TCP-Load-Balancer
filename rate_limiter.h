#include <iostream>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <string>

constexpr double MAX_TOKENS = 10.0; // Dung luong toi da cua thung the
constexpr double refill_rate = 2.0; // So the duoc nap vao moi giay

struct ClientBucket {
    double tokens;
    std::chrono::steady_clock::time_point last_refill;
};

std::unordered_map<std::string, ClientBucket> rate_limit_map;
std::mutex rate_limit_mutex;

bool check_limit_rate(const std::string &client_ip) {
    std::lock_guard<std::mutex> lock(rate_limit_mutex);
    auto now = std::chrono::steady_clock::now();

    // Check xem dia chi ip co phai lan dau ket noi vao backend hay khong
    if (rate_limit_map.find(client_ip) == rate_limit_map.end()) {
        rate_limit_map[client_ip] = {MAX_TOKENS, now};
    }
    // Tinh thoi gian troi qua tu lan kiem tra truoc (don vi: seconds)
    ClientBucket &bucket = rate_limit_map[client_ip];
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - bucket.last_refill).count();
    double seconds_elapsed = duration_ms / 1000.0;
    bucket.last_refill = now;

    // Bo sung the vao dua tren khoang thoi gian da troi qua
    bucket.tokens += seconds_elapsed * refill_rate;
    if (bucket.tokens > MAX_TOKENS) {
        bucket.tokens = MAX_TOKENS;
    }
    if (bucket.tokens >= 1.0) {
        bucket.tokens -= 1.0;
        return true;
    }
    return false;
}