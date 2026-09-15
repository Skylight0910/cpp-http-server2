#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "Channel.h"
#include "EventLoop.h"
#include "EventLoopThread.h"
#include "HttpRequest.h"
#include "HttpResponse.h"

namespace {

constexpr int64_t kIdleTimeoutMs = 5000;

void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl(F_GETFL)");
        exit(1);
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl(F_SETFL)");
        exit(1);
    }
}

void refreshIdleTimer(EventLoop *loop, const std::weak_ptr<Channel>& weakCh) {
    auto ch = weakCh.lock();
    if (!ch) return;

    int fd = ch->fd();
    loop->addTimer(fd, EventLoop::nowMs() + kIdleTimeoutMs, [weakCh]() {
        auto ch = weakCh.lock();
        if (!ch) return;

        std::cout << "[TIMER] idle connection timeout fd=" << ch->fd() << std::endl;
        ch->handleClose();
    });
}

} // namespace

int main() {
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    setNonBlocking(listenFd);
    int opt = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(listenFd, (struct sockaddr*)&addr, sizeof(addr));
    listen(listenFd, SOMAXCONN);

    std::cout << "Server started on port 8080" << std::endl;

    EventLoop mainLoop;
    const int numWorkers = 3;
    std::vector<std::unique_ptr<EventLoopThread>> threads;
    std::vector<EventLoop*> workerLoops;

    for (int i = 0; i < numWorkers; i++) {
        auto t = std::make_unique<EventLoopThread>();
        workerLoops.push_back(t->startLoop());
        threads.push_back(std::move(t));
    }

    std::atomic<int> nextWorker{0};
    Channel listenChannel(listenFd, &mainLoop);

    listenChannel.setReadCallback([&]() {
        // 非阻塞 accept：一次 EPOLLIN 可能积压多个连接，循环取直到 EAGAIN
        while (true) {
            struct sockaddr_in cliaddr;
            socklen_t clilen = sizeof(cliaddr);
            int clientFd = accept(listenFd, (struct sockaddr*)&cliaddr, &clilen);

            if (clientFd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // 本次连接已取完
                if (errno == EINTR) continue;                        // 被信号打断，重试
                std::cerr << "[ACCEPT] error: " << strerror(errno) << std::endl;
                break;
            }

            setNonBlocking(clientFd);

            int idx = nextWorker.fetch_add(1) % numWorkers;
            EventLoop *workerLoop = workerLoops[idx];
            std::cout << "New connection fd=" << clientFd << " -> worker" << idx << std::endl;

            workerLoop->runInLoop([workerLoop, clientFd]() {
                auto clientChannel = std::make_shared<Channel>(clientFd, workerLoop);
                workerLoop->holdChannel(clientChannel);
                // Channel 生命周期保护：回调链触发关闭时，保证本次事件处理完整结束
                clientChannel->tie(clientChannel);
                std::weak_ptr<Channel> weakCh = clientChannel;

                clientChannel->setReadCallback([weakCh, workerLoop]() {
                    auto ch = weakCh.lock();
                    if (!ch) return;

                    ssize_t n = ch->inputBuffer().readFromFd(ch->fd());

                    if (n > 0) {
                        refreshIdleTimer(workerLoop, weakCh);

                        const char* end = strstr(ch->inputBuffer().data(), "\r\n\r\n");
                        if (end) {
                            size_t requestLen = end - ch->inputBuffer().data() + 4;
                            std::string rawRequest =
                                ch->inputBuffer().retrieveAsString(requestLen);

                            HttpRequest request;
                            if (request.parse(rawRequest)) {
                                std::cout << "Request: " << request.method() << " "
                                          << request.path() << std::endl;

                                HttpResponse response;
                                if (request.path() == "/") {
                                    response.setStatus(200, "OK");
                                    response.addHeader("Content-Type", "text/html; charset=utf-8");
                                    response.setBody("<html><body><h1>Hello!</h1></body></html>");
                                } else {
                                    response.setStatus(404, "Not Found");
                                    response.addHeader("Content-Type", "text/html; charset=utf-8");
                                    response.setBody("<html><body><h1>404</h1></body></html>");
                                }

                                std::string responseStr = response.toString();
                                ch->sendData(responseStr);

                                // 数据全部直接发完才关闭写端；
                                // 有剩余在输出缓冲区时，等冲刷完由对端关闭/定时器兜底
                                if (ch->outputBuffer().empty()) {
                                    shutdown(ch->fd(), SHUT_WR);
                                }
                            }
                        }
                    } else if (n == 0) {
                        ch->handleClose();  // 对端正常关闭
                    } else {
                        // n < 0：EAGAIN 不是错误；真实错误才断连
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // LT 模式下一般不会走到这里，防御性保留连接
                        } else {
                            std::cerr << "[READ] error fd=" << ch->fd()
                                      << " errno=" << strerror(errno) << std::endl;
                            ch->handleClose();
                        }
                    }
                });

                clientChannel->setWriteCallback([weakCh]() {
                    auto ch = weakCh.lock();
                    if (!ch) return;
                    ch->flushOutput();
                });

                clientChannel->setCloseCallback([workerLoop, weakCh]() {
                    workerLoop->runInLoop([workerLoop, weakCh]() {
                        auto ch = weakCh.lock();
                        if (!ch) return;

                        std::cout << "[CLOSE] remove fd=" << ch->fd() << std::endl;
                        workerLoop->removeChannel(ch.get());
                    });
                });

                refreshIdleTimer(workerLoop, weakCh);
                clientChannel->enableReading();
            });
        }
    });

    listenChannel.enableReading();
    mainLoop.loop();

    close(listenFd);
    return 0;
}
