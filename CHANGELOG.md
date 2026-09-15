# 更新日志

本项目的所有显著变更都记录在此文件中。
格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循语义化版本。

## [0.2.0] - 2026-09-15

### 新增

- 全非阻塞 IO：监听与客户端 fd 均设置 `O_NONBLOCK`，`accept` 循环取空直至 `EAGAIN`，慢客户端不再阻塞 worker 线程。
- 输出缓冲区 + EPOLLOUT 写完成机制：`send` 遇内核缓冲区满时缓存剩余数据，由可写事件驱动冲刷，杜绝响应截断。
- 请求头 8KB 上限防护：超限返回 `431 Request Header Fields Too Large` 并断连（flush-before-close），防止恶意连接耗尽内存。
- Channel 生命周期保护（muduo tie 模式）：事件回调执行期间对象不被销毁，杜绝 use-after-free。
- `.gitignore` 与三档并发压测数据（见 README 性能基准）。

### 修复

- `send` 使用 `MSG_NOSIGNAL`，对端异常关闭不再触发 SIGPIPE 杀死进程。
- `recv` 结果三分类（数据 / 对端关闭 / EAGAIN 保留连接），EAGAIN 不再被误判为连接错误。

### 变更

- `listen` backlog 从 5 提升至 `SOMAXCONN`，避免突发连接被内核丢弃。

## [0.1.0] - 2026-06-02

### 初始版本

- 基于 epoll 的 Reactor 模式，LT 水平触发。
- One Loop Per Thread 多线程架构：主线程 accept，轮询分发给 worker 线程。
- HTTP/1.1 请求解析（请求行 + 请求头，按 `\r\n\r\n` 切分处理 TCP 粘包）。
- 基于 multiset 的定时器，空闲连接 5 秒超时断开。
- eventfd 跨线程任务唤醒（runInLoop + queueInLoop）。
