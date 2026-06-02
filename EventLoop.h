#pragma once
#include <sys/epoll.h>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <mutex>
#include <thread>
#include <set>
#include "Timer.h"

class Channel;

class EventLoop {
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void loop();

    bool isInLoopThread() const;
    void runInLoop(Functor cb);
    void queueInLoop(Functor cb);

    void updateChannel(Channel *channel);
    void removeChannel(Channel *channel);
    void holdChannel(std::shared_ptr<Channel> channel);

    void addTimer(int id, int64_t expireTime, Timer::TimerCallback cb);
    void removeTimer(int id);
    void handleExpiredTimers();

    static int64_t nowMs();

private:
    void doPendingFunctors();
    void wakeup();
    void removeTimerInLock(int id);

    int epollFd_;
    std::vector<struct epoll_event> events_;
    std::map<int, bool> fdInEpoll_;
    std::map<int, std::shared_ptr<Channel>> activeChannels_;

    std::thread::id threadId_;
    std::vector<Functor> pendingFunctors_;
    std::mutex mutex_;
    int wakeupFd_;

    std::multiset<std::pair<int64_t, int>> timers_;
    std::map<int, std::shared_ptr<Timer>> timerMap_;
    std::mutex timerMutex_;  // 保护 timers_ 和 timerMap_

    static const int kInitEventsSize = 128;
};
