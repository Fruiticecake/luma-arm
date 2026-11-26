#include "luma/hal/posix_serial_transport.hpp"

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

namespace luma::hal {

namespace {

speed_t toTermiosBaud(uint32_t baud) noexcept {
    switch (baud) {
        case 115200:
            return B115200;
        case 230400:
            return B230400;
        case 460800:
            return B460800;
        case 500000:
            return B500000;
        case 921600:
            return B921600;
        case 1000000:
            return B1000000;
        case 1500000:
            return B1500000;
        default:
            return 0;
    }
}

}  // namespace

PosixSerialTransport::PosixSerialTransport(std::string device_path, uint32_t baudrate)
    : device_path_(std::move(device_path)), baudrate_(baudrate) {}

PosixSerialTransport::~PosixSerialTransport() { close(); }

core::Status PosixSerialTransport::open() {
    if (fd_ >= 0) {
        return core::Status::Ok();
    }

    const speed_t speed = toTermiosBaud(baudrate_);
    if (speed == 0) {
        return core::Status::Error(core::ErrorCode::kInvalidArg,
                                   "Unsupported baudrate: " + std::to_string(baudrate_));
    }

    const int fd = ::open(device_path_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return core::Status::Error(core::ErrorCode::kIoError,
                                   "open(" + device_path_ + ") failed: " + std::strerror(errno));
    }

    termios tty{};
    if (::tcgetattr(fd, &tty) != 0) {
        const std::string err = std::strerror(errno);
        ::close(fd);
        return core::Status::Error(core::ErrorCode::kIoError, "tcgetattr failed: " + err);
    }

    ::cfmakeraw(&tty);
    tty.c_cflag = (tty.c_cflag & ~static_cast<tcflag_t>(CSIZE)) | CS8 | CREAD | CLOCAL;
    tty.c_cflag &= ~static_cast<tcflag_t>(PARENB | PARODD | CSTOPB | CRTSCTS);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    ::cfsetispeed(&tty, speed);
    ::cfsetospeed(&tty, speed);

    if (::tcsetattr(fd, TCSANOW, &tty) != 0) {
        const std::string err = std::strerror(errno);
        ::close(fd);
        return core::Status::Error(core::ErrorCode::kIoError, "tcsetattr failed: " + err);
    }

    ::tcflush(fd, TCIOFLUSH);
    fd_ = fd;
    return core::Status::Ok();
}

void PosixSerialTransport::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

core::Status PosixSerialTransport::writeAll(const uint8_t* data, std::size_t len) {
    if (fd_ < 0) {
        return core::Status::Error(core::ErrorCode::kNotConnected, "Serial port not open");
    }

    std::size_t sent = 0;
    while (sent < len) {
        const ssize_t n = ::write(fd_, data + sent, len - sent);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            pollfd pfd{fd_, POLLOUT, 0};
            if (::poll(&pfd, 1, 100) <= 0) {
                return core::Status::Error(core::ErrorCode::kTimeout, "Serial write timed out");
            }
            continue;
        }
        return core::Status::Error(core::ErrorCode::kIoError,
                                   std::string("Serial write failed: ") + std::strerror(errno));
    }
    return core::Status::Ok();
}

core::Status PosixSerialTransport::readSome(uint8_t* buf, std::size_t max_len, int timeout_ms,
                                            std::size_t& out_read) {
    out_read = 0;
    if (fd_ < 0) {
        return core::Status::Error(core::ErrorCode::kNotConnected, "Serial port not open");
    }

    pollfd pfd{fd_, POLLIN, 0};
    const int ret = ::poll(&pfd, 1, timeout_ms);
    if (ret == 0) {
        return core::Status::Error(core::ErrorCode::kTimeout, "Serial read timed out");
    }
    if (ret < 0) {
        if (errno == EINTR) {
            return core::Status::Error(core::ErrorCode::kTimeout, "Serial poll interrupted");
        }
        return core::Status::Error(core::ErrorCode::kIoError,
                                   std::string("Serial poll failed: ") + std::strerror(errno));
    }

    const ssize_t n = ::read(fd_, buf, max_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return core::Status::Error(core::ErrorCode::kTimeout, "Serial read would block");
        }
        return core::Status::Error(core::ErrorCode::kIoError,
                                   std::string("Serial read failed: ") + std::strerror(errno));
    }
    if (n == 0) {
        return core::Status::Error(core::ErrorCode::kIoError, "Serial link lost (EOF)");
    }
    out_read = static_cast<std::size_t>(n);
    return core::Status::Ok();
}

}  // namespace luma::hal
