#pragma once

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace local_socket {

class fd {
    int value_ = -1;
public:
    explicit fd(int value = -1) : value_(value) {}
    ~fd() { if (value_ >= 0) ::close(value_); }
    fd(const fd &) = delete;
    fd & operator=(const fd &) = delete;
    fd(fd && other) noexcept : value_(other.value_) { other.value_ = -1; }
    int get() const { return value_; }
};

inline sockaddr_un address(const std::string & path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.empty() || path[0] != '/' || path.size() >= sizeof(addr.sun_path))
        throw std::runtime_error("socket path must be absolute and fit AF_UNIX");
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    return addr;
}

inline void ready(int socket, short events, std::chrono::steady_clock::time_point deadline) {
    while (true) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) throw std::runtime_error("socket operation timed out");
        pollfd entry{socket, events, 0};
        int result = ::poll(&entry, 1, static_cast<int>(left));
        if (result > 0) {
            if (entry.revents & POLLNVAL) throw std::runtime_error("invalid socket");
            return; // recv/send reports EOF or error after POLLHUP/POLLERR.
        }
        if (result == 0) throw std::runtime_error("socket operation timed out");
        if (errno != EINTR) throw std::runtime_error("socket poll failed");
    }
}

inline void write_all(int socket, const void * data, size_t size,
                      std::chrono::steady_clock::time_point deadline) {
    auto * bytes = static_cast<const char *>(data);
    while (size) {
        ready(socket, POLLOUT, deadline);
        ssize_t done = ::send(socket, bytes, size, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (done > 0) { bytes += done; size -= static_cast<size_t>(done); }
        else if (done < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        else throw std::runtime_error("socket write failed or peer disconnected");
    }
}

inline void read_all(int socket, void * data, size_t size,
                     std::chrono::steady_clock::time_point deadline) {
    auto * bytes = static_cast<char *>(data);
    while (size) {
        ready(socket, POLLIN, deadline);
        ssize_t done = ::recv(socket, bytes, size, MSG_DONTWAIT);
        if (done > 0) { bytes += done; size -= static_cast<size_t>(done); }
        else if (done < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        else throw std::runtime_error("socket read failed or peer disconnected");
    }
}

inline void send_frame(int socket, const std::string & text, size_t maximum, int seconds) {
    if (text.empty() || text.size() > maximum)
        throw std::runtime_error("socket frame size invalid");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    uint32_t length = htonl(static_cast<uint32_t>(text.size()));
    write_all(socket, &length, sizeof(length), deadline);
    write_all(socket, text.data(), text.size(), deadline);
}

inline std::string receive_frame(int socket, size_t maximum, int seconds) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    uint32_t encoded = 0;
    read_all(socket, &encoded, sizeof(encoded), deadline);
    size_t length = ntohl(encoded);
    if (length == 0 || length > maximum)
        throw std::runtime_error("socket frame size invalid");
    std::string text(length, '\0');
    read_all(socket, text.data(), length, deadline);
    return text;
}

inline fd connect_retry(const std::string & path, int milliseconds = 2000) {
    auto addr = address(path);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (true) {
        fd client(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
        if (client.get() < 0) throw std::runtime_error("cannot create Unix socket");
        if (::connect(client.get(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0)
            return client;
        int error = errno;
        if (error != ENOENT && error != ECONNREFUSED && error != EAGAIN)
            throw std::runtime_error(std::string("socket connect failed: ") + std::strerror(error));
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("resident worker did not become ready");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

inline fd listen_private(const std::string & path) {
    auto addr = address(path);
    struct stat status{};
    if (::lstat(path.c_str(), &status) == 0)
        throw std::runtime_error("socket path already exists; refusing to replace it");
    if (errno != ENOENT) throw std::runtime_error("cannot inspect socket path");
    fd server(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (server.get() < 0) throw std::runtime_error("cannot create Unix socket");
    mode_t previous = ::umask(0077);
    int result = ::bind(server.get(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    int error = errno;
    ::umask(previous);
    if (result != 0)
        throw std::runtime_error(std::string("socket bind failed: ") + std::strerror(error));
    if (::chmod(path.c_str(), 0600) != 0 || ::listen(server.get(), 16) != 0) {
        ::unlink(path.c_str());
        throw std::runtime_error("cannot secure or listen on Unix socket");
    }
    return server;
}

} // namespace local_socket
