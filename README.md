# C++ 高性能 HTTP 服务器

基于 **epoll** 的**多线程** HTTP 服务器，采用 **One Loop Per Thread** 架构，支持高并发连接处理与 HTTP/1.1 请求解析。

## 技术亮点

- **I/O 多路复用**：基于 epoll 实现 Reactor 模式，LT 水平触发
- **多线程模型**：One Loop Per Thread，主线程只负责 accept，轮询分发给工作线程
- **无锁化设计**：每个连接从生到死都在固定线程内处理，避免线程间数据竞争
- **智能指针管理生命周期**：shared_ptr / weak_ptr 管理 Channel 对象，防止悬垂指针和循环引用
- **应用层 Buffer**：解决 TCP 粘包问题，按 `\r\n\r\n` 切分完整 HTTP 请求
- **定时器**：基于 multiset 的超时管理，空闲连接自动断开
- **跨线程调度**：runInLoop + eventfd 实现线程安全的任务分发

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
