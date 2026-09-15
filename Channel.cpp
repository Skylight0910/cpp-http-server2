#include "Channel.h"
#include "EventLoop.h"
#include <iostream>
#include <unistd.h>
#include <cerrno>

Channel::Channel(int fd, EventLoop *loop)
    : fd_(fd)
    , loop_(loop)
    , events_(0)
{
}

Channel::~Channel() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void Channel::setReadCallback(EventCallback cb) { readCallback_ = std::move(cb); }
void Channel::setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
void Channel::setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }

void Channel::enableReading() {
    events_ |= EPOLLIN;
    update();
}

void Channel::disableReading() {
    events_ &= ~EPOLLIN;
    update();
}

void Channel::enableWriting() {
    events_ |= EPOLLOUT;
    update();
}

void Channel::disableWriting() {
    events_ &= ~EPOLLOUT;
    update();
}

void Channel::update() {
    loop_->updateChannel(this);
}

void Channel::handleEvent(uint32_t revents) {
    if (revents & (EPOLLHUP | EPOLLERR)) {
        handleClose();
        return;
    }
    if (revents & EPOLLIN) {
        if (readCallback_) readCallback_();
    }
    if (revents & EPOLLOUT) {
        if (writeCallback_) writeCallback_();
    }
}

void Channel::handleClose() {
    if (closeCallback_) {
        EventCallback cb;
        std::swap(cb, closeCallback_);
        if (cb) cb();
    }
}

void Channel::sendData(const std::string& data) {
    if (data.empty()) return;

    // 输出缓冲区还有未发完的数据：新数据必须排队，保证发送顺序
    if (!outputBuffer_.empty()) {
        outputBuffer_.append(data.data(), data.size());
        return;
    }

    size_t remaining = data.size();
    const char* ptr = data.data();

    while (remaining > 0) {
        ssize_t n = ::send(fd_, ptr, remaining, MSG_NOSIGNAL);
        if (n > 0) {
            remaining -= static_cast<size_t>(n);
            ptr += n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // 内核发送缓冲区满：剩余数据进应用层缓冲，注册 EPOLLOUT 等可写事件
            outputBuffer_.append(ptr, remaining);
            enableWriting();
            return;
        }
        // 真正的错误（EPIPE / ECONNRESET 等）
        handleClose();
        return;
    }
}

void Channel::flushOutput() {
    while (!outputBuffer_.empty()) {
        ssize_t n = ::send(fd_, outputBuffer_.data(), outputBuffer_.size(), MSG_NOSIGNAL);
        if (n > 0) {
            outputBuffer_.retrieve(static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;  // 缓冲区仍满，等下一次 EPOLLOUT
        }
        handleClose();
        return;
    }
    disableWriting();  // 全部发完，取消对 EPOLLOUT 的关注
}
