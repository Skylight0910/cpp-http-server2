# cpp-http-server2

一个基于 Linux epoll 的多线程 HTTP 服务器原型，采用 **One Loop Per Thread** 架构，用于验证高并发连接处理、非阻塞 IO、应用层缓冲和连接生命周期管理。

当前版本面向学习和面试展示，重点保证核心网络路径清晰、可构建、可测试、可复现实验；它不是生产级 Web 服务器。

## 核心特性

- **Reactor + One Loop Per Thread**：主线程负责 `accept`，工作线程各自维护一个 `EventLoop`，连接创建后固定在同一个 worker 内处理，避免连接状态跨线程迁移。
- **非阻塞 IO**：监听 socket 和客户端 socket 均设置 `O_NONBLOCK`；`accept` 循环取到 `EAGAIN`，`recv/send` 遇到内核缓冲区限制时不会阻塞 worker。
- **应用层缓冲**：输入缓冲按 `\r\n\r\n` 切分完整请求头，输出缓冲在内核发送缓冲区满时暂存数据，并等待 `EPOLLOUT` 继续冲刷。
- **请求防护**：请求头大小可配置，超过限制返回 `431`；非法请求行或请求头返回 `400`。
- **方法校验**：当前仅支持 `GET`，其他方法返回 `405` 并携带 `Allow: GET`。
- **HTTP/1.1 keep-alive**：默认复用连接，支持同一连接顺序请求和 pipeline 请求；`Connection: close` 时关闭。
- **空闲连接超时**：每个连接使用可刷新定时器，超时后主动关闭，避免慢客户端长期占用资源。
- **生命周期保护**：`shared_ptr` 管理 `Channel`，回调通过 `weak_ptr` 延迟锁定，`tie` 保证事件回调执行期间对象不被销毁。
- **跨线程任务分发**：主线程通过 `runInLoop + eventfd` 将新连接安全交给指定 worker。

## 架构

```text
main thread
  ├─ listen socket (non-blocking + epoll)
  └─ accept loop
        │ round-robin
        ▼
worker EventLoop 1 ... N
  ├─ epoll wait
  ├─ request parse
  ├─ app input/output buffer
  ├─ timer queue
  └─ close / timeout cleanup
```

## 构建

依赖：Linux、CMake 3.16+、C++17 编译器。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## 运行

```bash
./build/http_server --port 8080 --workers 1
```

可用参数：

| 参数 | 默认值 | 说明 |
|---|---:|---|
| `-p, --port` | `8080` | 监听端口 |
| `-w, --workers` | `1` | worker EventLoop 数量 |
| `--idle-timeout-ms` | `5000` | 空闲连接超时时间 |
| `--max-header-size` | `8192` | 请求头上限 |

快速验证：

```bash
curl -i http://127.0.0.1:8080/
```

## 测试

```bash
ctest --test-dir build --output-on-failure
```

也可以在 Linux 环境执行完整冒烟测试：

```bash
bash scripts/smoke_test.sh
```

协议行为测试：

```bash
bash scripts/protocol_test.sh
```

当前测试覆盖：

- `Buffer` 追加、检索、清理和 CRLF 查找；
- `HttpRequest` 合法请求、非法请求行、非法请求头解析；
- `HttpResponse` 状态行、头部、`Content-Length` 和连接复用语义；
- 协议脚本覆盖同一连接顺序复用、pipeline、`Connection: close`、`400`、`431` 和空闲超时。

## 性能基准

测试环境：VMware Ubuntu 虚拟机，2 vCPU，1 worker 线程，回环地址，`ab -n 100000`。

### 短连接基线

| 并发数 | QPS | P50 (ms) | P99 (ms) | 失败请求数 |
|---:|---:|---:|---:|---:|
| 100 | 13,170.22 | 7 | 14 | 0 |
| 200 | 13,027.71 | 15 | 21 | 0 |
| 500 | 13,284.12 | 37 | 46 | 0 |

### HTTP/1.1 keep-alive

| 并发数 | QPS | P50 (ms) | P99 (ms) | 失败请求数 | Keep-Alive 请求数 |
|---:|---:|---:|---:|---:|---:|
| 100 | 56,891.59 | 2 | 5 | 0 | 100,000 |
| 200 | 54,796.02 | 3 | 8 | 0 | 100,000 |
| 500 | 56,135.50 | 9 | 13 | 0 | 100,000 |

相比短连接基线，keep-alive 在 100/200/500 并发下的 QPS 分别提升约 **4.3x / 4.2x / 4.2x**，P99 从 14–46ms 降至 5–13ms。该结果是虚拟机回环网络下的学习型基准，用于对比连接策略和参数变化，不能代表生产环境吞吐。当前小响应场景下，1 worker 优于 2/3/4 worker；原因是在 2 vCPU 环境中，主线程 accept 与单个 worker 已能较好利用 CPU，更多 worker 会引入额外的跨线程唤醒和上下文切换开销。

## 当前限制

- 尚未解析请求体；带非零 `Content-Length` 的请求会在响应后关闭连接；
- 定时器使用 `multiset`，当前规模够用，后续可优化为小顶堆或时间轮；
- 日志仍使用标准输出，后续可替换为结构化异步日志；
- 尚缺压力测试脚本、AddressSanitizer/ThreadSanitizer CI 和端到端集成测试。

## 后续计划

1. 支持请求体解析和静态文件服务；
2. 增加连接数、QPS、错误码等运行时指标；
3. 引入 AddressSanitizer/ThreadSanitizer CI；
4. 优化定时器数据结构，并补充优雅退出流程。
