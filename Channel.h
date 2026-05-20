#pragma once
#include <sys/epoll.h>
#include <functional>
#include "Buffer.h"

class EventLoop;

class Channel {
public:
    using EventCallback = std::function<void()>;

    Channel(int fd, EventLoop *loop);
    ~Channel();

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    void setReadCallback(EventCallback cb);
    void setWriteCallback(EventCallback cb);
    void setCloseCallback(EventCallback cb);

    void enableReading();
    void disableReading();
    void enableWriting();
    void disableWriting();

    int fd() const { return fd_; }
    int events() const { return events_; }
    
    void handleEvent(uint32_t revents);
    void handleClose();  // 新增：安全触发关闭流程

    Buffer& inputBuffer() { return inputBuffer_; }

private:
    void update();

    int fd_;
    EventLoop *loop_;
    int events_;

    EventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    
    Buffer inputBuffer_;
};
