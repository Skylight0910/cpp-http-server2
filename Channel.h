#pragma once
#include <sys/epoll.h>
#include <functional>
#include <memory>
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

    // 生命周期保护：owner 用自己的 shared_ptr 调用，
    // handleEvent 执行期间保证本对象不被销毁
    void tie(const std::shared_ptr<void>& obj);

    void enableReading();
    void disableReading();
    void enableWriting();
    void disableWriting();

    int fd() const { return fd_; }
    int events() const { return events_; }
    
    void handleEvent(uint32_t revents);
    void handleClose();  // 新增：安全触发关闭流程

    Buffer& inputBuffer() { return inputBuffer_; }
    Buffer& outputBuffer() { return outputBuffer_; }
    void sendData(const std::string& data);
    void flushOutput();

private:
    void update();

    int fd_;
    EventLoop *loop_;
    int events_;

    EventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;

    std::weak_ptr<void> tie_;
    bool tied_ = false;
    
    Buffer inputBuffer_;
    Buffer outputBuffer_;
};
