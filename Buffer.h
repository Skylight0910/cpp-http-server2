#pragma once
#include <string>
#include <sys/socket.h>
#include <unistd.h>

class Buffer {
public:
    Buffer();

    ssize_t readFromFd(int fd);
    const char* data() const;
    size_t size() const;
    bool empty() const { return buffer_.empty(); }
    std::string retrieveAsString(size_t len);
    void retrieve(size_t len);
    void append(const char* data, size_t len);
    void clear();
    int findCRLF() const;

private:
    std::string buffer_;
};
