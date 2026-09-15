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

void Channel::tie(const std::shared_ptr<void>& obj) {
    tie_ = obj;
    tied_ = true;
}

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
    // 生命周期守卫：回调链中可能触发关闭并从 owner 的 map 中移除本对象，
    // 先把 weak_ptr 提升为 shared_ptr，保证 handleEvent 返回前对象不被销毁
    std::shared_ptr<void> guard;
    if (tied_) {
        guard = tie_.lock();
        if (!guard) return;  // 对象已被销毁，忽略过期事件
    }

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

    if (rejected_ || closeAfterFlush_) {
        handleClose();  // 拒绝响应已全部送达，现在才真正关闭
    }
}

void Channel::reject(const std::string& response) {
    rejected_ = true;
    disableReading();   // 冻结输入：摘掉 EPOLLIN，Buffer 从此封顶
    sendData(response); // 尽力直发；内核缓冲满时剩余进输出缓冲区

    if (outputBuffer().empty()) {
        handleClose();  // 已全部发出，立即关闭
    }
    // 否则等 EPOLLOUT 冲刷完，由 flushOutput 触发 handleClose
}
