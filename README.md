# C++ 高性能 HTTP 服务器

基于 **epoll** 的**多线程** HTTP 服务器，采用 **One Loop Per Thread** 架构，支持高并发连接处理与 HTTP/1.1 请求解析。

## 技术亮点

- **I/O 多路复用**：基于 epoll 实现 Reactor 模式，LT 水平触发
- **多线程模型**：One Loop Per Thread，主线程只负责 accept，轮询分发给工作线程
- **全非阻塞 IO**：监听与客户端 fd 均设置 `O_NONBLOCK`，`accept` 循环取空直至 `EAGAIN`，慢客户端不会阻塞 worker 线程
- **完整的 EAGAIN 处理**：`recv` 严格区分「有数据 / 对端关闭 / EAGAIN 保留连接」三种情况；`send` 遇内核缓冲区满时将剩余数据写入应用层输出缓冲区并注册 `EPOLLOUT`，由可写事件驱动冲刷
- **SIGPIPE 防护**：`send` 使用 `MSG_NOSIGNAL`，对端异常关闭不会杀死进程
- **高并发就绪**：`listen` backlog 提升至 `SOMAXCONN`，避免突发连接被内核丢弃
- **无锁化设计**：每个连接从生到死都在固定线程内处理，避免线程间数据竞争
- **智能指针管理生命周期**：shared_ptr / weak_ptr 管理 Channel 对象，防止悬垂指针和循环引用
- **应用层 Buffer**：解决 TCP 粘包问题，按 `\r\n\r\n` 切分完整 HTTP 请求
- **定时器**：基于 multiset 的超时管理，空闲连接自动断开
- **跨线程调度**：runInLoop + eventfd 实现线程安全的任务分发

## 性能基准

测试环境：VMware 虚拟机（Ubuntu），3 worker 线程，回环地址，`ab -n 10000`。

| 并发数 | QPS | P50 (ms) | P99 (ms) | 失败请求数 |
-------:|--------:|----------:|----------:|-----------:|
| 100 | 8842.4 | 11 | 27 | 0 |
| 200 | 8645.8 | 22 | 57 | 0 |
| 500 | 8552.3 | 45 | 156 | 0 |

> 当前版本每个请求使用独立连接（HTTP/1.0 语义），keep-alive 复用为下一阶段计划。

## 快速开始

### 编译

```bash
g++ -std=c++17 -O2 -o server \
    server.cpp \
    Buffer.cpp \
    Channel.cpp \
    EventLoop.cpp \
    EventLoopThread.cpp \
    HttpRequest.cpp \
    HttpResponse.cpp \
    -lpthread
