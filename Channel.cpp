#include "Channel.h"
#include "EventLoop.h"
#include <iostream>
#include <unistd.h>

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
