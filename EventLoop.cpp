#include "EventLoop.h"
#include "Channel.h"
#include <sys/eventfd.h>
#include <sys/time.h>
#include <unistd.h>
#include <iostream>

int64_t EventLoop::nowMs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

EventLoop::EventLoop()
    : epollFd_(epoll_create(1))
    , events_(kInitEventsSize)
    , threadId_(std::this_thread::get_id())
    , wakeupFd_(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK))
{
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = nullptr;
    epoll_ctl(epollFd_, EPOLL_CTL_ADD, wakeupFd_, &ev);
}

EventLoop::~EventLoop() {
    close(epollFd_);
    close(wakeupFd_);
}

bool EventLoop::isInLoopThread() const {
    return threadId_ == std::this_thread::get_id();
}

void EventLoop::runInLoop(Functor cb) {
    if (isInLoopThread()) {
        cb();
    } else {
        queueInLoop(std::move(cb));
    }
}

void EventLoop::queueInLoop(Functor cb) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.push_back(std::move(cb));
    }
    if (!isInLoopThread()) {
        wakeup();
    }
}

void EventLoop::wakeup() {
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof(one));
    (void)n;
}

void EventLoop::doPendingFunctors() {
    std::vector<Functor> functors;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }
    for (const auto& func : functors) {
        func();
    }
}

void EventLoop::loop() {
    while (true) {
        int timeoutMs = 1000;
        {
            std::lock_guard<std::mutex> lock(timerMutex_);
            if (!timers_.empty()) {
                int64_t nextExpire = timers_.begin()->first;
                int64_t now = nowMs();
                timeoutMs = static_cast<int>(nextExpire - now);
                if (timeoutMs < 0) timeoutMs = 0;
                if (timeoutMs > 1000) timeoutMs = 1000;
            }
        }

        int numEvents = epoll_wait(epollFd_, events_.data(), events_.size(), timeoutMs);

        for (int i = 0; i < numEvents; i++) {
            if (events_[i].data.ptr == nullptr) {
                uint64_t buf;
                ssize_t n = read(wakeupFd_, &buf, sizeof(buf));
                (void)n;
                continue;
            }
            auto channel = static_cast<Channel*>(events_[i].data.ptr);
            channel->handleEvent(events_[i].events);
        }

        doPendingFunctors();
        handleExpiredTimers();
    }
}

void EventLoop::updateChannel(Channel *channel) {
    int fd = channel->fd();
    struct epoll_event ev;
    ev.events = channel->events();
    ev.data.ptr = channel;

    int op;
    bool isInEpoll = fdInEpoll_[fd];

    if (channel->events() == 0) {
        op = EPOLL_CTL_DEL;
        fdInEpoll_[fd] = false;
    } else if (isInEpoll) {
        op = EPOLL_CTL_MOD;
    } else {
        op = EPOLL_CTL_ADD;
        fdInEpoll_[fd] = true;
    }

    epoll_ctl(epollFd_, op, fd, &ev);
}

void EventLoop::removeChannel(Channel *channel) {
    int fd = channel->fd();

    removeTimer(fd);
    
    if (fdInEpoll_.find(fd) == fdInEpoll_.end() || !fdInEpoll_[fd]) {
        std::cout << "  -> 跳过重复删除 fd=" << fd << std::endl;
        activeChannels_.erase(fd);
        return;
    }

    std::cout << "removeChannel fd=" << fd << std::endl;
    
    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
    fdInEpoll_[fd] = false;
    std::cout << "  -> epoll DEL" << std::endl;
    
    activeChannels_.erase(fd);
    std::cout << "  -> 从activeChannels_移除, 剩余活跃连接: " << activeChannels_.size() << std::endl;
}

void EventLoop::holdChannel(std::shared_ptr<Channel> channel) {
    activeChannels_[channel->fd()] = channel;
}

void EventLoop::addTimer(int id, int64_t expireTime, Timer::TimerCallback cb) {
    std::lock_guard<std::mutex> lock(timerMutex_);
    removeTimerInLock(id);
    auto timer = std::make_shared<Timer>(expireTime, std::move(cb));
    timerMap_[id] = timer;
    timers_.insert({expireTime, id});
}

void EventLoop::removeTimer(int id) {
    std::lock_guard<std::mutex> lock(timerMutex_);
    removeTimerInLock(id);
}

void EventLoop::removeTimerInLock(int id) {
    auto it = timerMap_.find(id);
    if (it != timerMap_.end()) {
        int64_t expireTime = it->second->expireTime();
        auto range = timers_.equal_range({expireTime, id});
        for (auto iter = range.first; iter != range.second; ++iter) {
            if (iter->second == id) {
                timers_.erase(iter);
                break;
            }
        }
        timerMap_.erase(it);
    }
}

void EventLoop::handleExpiredTimers() {
    std::vector<Timer::TimerCallback> expiredCallbacks;
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        int64_t now = nowMs();
        while (!timers_.empty()) {
            auto it = timers_.begin();
            if (it->first > now) break;
            int id = it->second;
            timers_.erase(it);
            auto timerIt = timerMap_.find(id);
            if (timerIt != timerMap_.end()) {
                expiredCallbacks.push_back(timerIt->second->callback());
                timerMap_.erase(timerIt);
            }
        }
    } // 锁释放

    // 在锁外执行回调，彻底避免死锁
    for (auto& cb : expiredCallbacks) {
        if (cb) cb();
    }
}
