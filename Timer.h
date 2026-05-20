#pragma once
#include <functional>
#include <memory>

class Timer {
public:
    using TimerCallback = std::function<void()>;

    Timer(int64_t expireTime, TimerCallback cb)
        : expireTime_(expireTime)
        , callback_(std::move(cb))
    {}

    void run() const {
        if (callback_) callback_();
    }

    int64_t expireTime() const { return expireTime_; }
    const TimerCallback& callback() const { return callback_; }  // 新增

private:
    int64_t expireTime_;
    TimerCallback callback_;
};
