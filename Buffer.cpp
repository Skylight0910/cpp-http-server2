#include "Buffer.h"
#include <iostream>

Buffer::Buffer() {
    buffer_.reserve(4096);
}

ssize_t Buffer::readFromFd(int fd) {
    char tmp[4096];
    ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
    if (n > 0) {
        buffer_.append(tmp, n);
    }
    return n;
}

const char* Buffer::data() const {
    return buffer_.data();
}

size_t Buffer::size() const {
    return buffer_.size();
}

std::string Buffer::retrieveAsString(size_t len) {
    if (len > buffer_.size()) len = buffer_.size();
    std::string result(buffer_.begin(), buffer_.begin() + len);
    buffer_.erase(0, len);
    return result;
}

void Buffer::clear() {
    buffer_.clear();
}

int Buffer::findCRLF() const {
    size_t pos = buffer_.find("\r\n");
    if (pos == std::string::npos) return -1;
    return static_cast<int>(pos);
}
