#include "luma/hal/tcp_transport.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

namespace luma::hal {

TcpTransport::TcpTransport(std::string host, uint16_t port, int connect_timeout_ms)
    : host_(std::move(host)), port_(port), connect_timeout_ms_(connect_timeout_ms) {}

TcpTransport::~TcpTransport() { close(); }

core::Status TcpTransport::open() {
    if (fd_ >= 0) {
        return core::Status::Ok();
    }

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return core::Status::Error(core::ErrorCode::kIoError,
                                   std::string("socket() failed: ") + std::strerror(errno));
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        return core::Status::Error(core::ErrorCode::kInvalidArg, "Invalid IPv4 address: " + host_);
    }

    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int ret = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        const std::string err = std::strerror(errno);
        ::close(fd);
        return core::Status::Error(core::ErrorCode::kIoError, "connect() failed: " + err);
    }

    if (ret != 0) {
        pollfd pfd{fd, POLLOUT, 0};
        ret = ::poll(&pfd, 1, connect_timeout_ms_);
        if (ret <= 0) {
            ::close(fd);
            return core::Status::Error(core::ErrorCode::kTimeout, "TCP connect timed out: " + host_);
        }
        int so_err = 0;
        socklen_t so_len = sizeof(so_err);
        ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &so_len);
        if (so_err != 0) {
            ::close(fd);
            return core::Status::Error(core::ErrorCode::kIoError,
                                       std::string("TCP connect error: ") + std::strerror(so_err));
        }
    }

    const int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    fd_ = fd;
    return core::Status::Ok();
}

void TcpTransport::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

core::Status TcpTransport::writeAll(const uint8_t* data, std::size_t len) {
    if (fd_ < 0) {
        return core::Status::Error(core::ErrorCode::kNotConnected, "TCP not connected");
    }

    std::size_t sent = 0;
    while (sent < len) {
        const ssize_t n = ::send(fd_, data + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            pollfd pfd{fd_, POLLOUT, 0};
            if (::poll(&pfd, 1, 200) <= 0) {
                return core::Status::Error(core::ErrorCode::kTimeout, "TCP send timed out");
            }
            continue;
        }
        return core::Status::Error(core::ErrorCode::kIoError,
                                   std::string("TCP send failed: ") + std::strerror(errno));
    }
    return core::Status::Ok();
}

core::Status TcpTransport::readSome(uint8_t* buf, std::size_t max_len, int timeout_ms,
                                    std::size_t& out_read) {
    out_read = 0;
    if (fd_ < 0) {
        return core::Status::Error(core::ErrorCode::kNotConnected, "TCP not connected");
    }

    pollfd pfd{fd_, POLLIN, 0};
    const int ret = ::poll(&pfd, 1, timeout_ms);
    if (ret == 0) {
        return core::Status::Error(core::ErrorCode::kTimeout, "TCP read timed out");
    }
    if (ret < 0) {
        return core::Status::Error(core::ErrorCode::kIoError,
                                   std::string("TCP poll failed: ") + std::strerror(errno));
    }

    const ssize_t n = ::recv(fd_, buf, max_len, 0);
    if (n == 0) {
        return core::Status::Error(core::ErrorCode::kIoError, "TCP peer closed connection");
    }
    if (n < 0) {
        return core::Status::Error(core::ErrorCode::kIoError,
                                   std::string("TCP recv failed: ") + std::strerror(errno));
    }
    out_read = static_cast<std::size_t>(n);
    return core::Status::Ok();
}

}  // namespace luma::hal
