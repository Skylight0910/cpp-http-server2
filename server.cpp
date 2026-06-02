#include <atomic>
#include <cstdint>
#include <cstring>
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
    int opt = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(listenFd, (struct sockaddr*)&addr, sizeof(addr));
    listen(listenFd, 5);

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
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);
        int clientFd = accept(listenFd, (struct sockaddr*)&cliaddr, &clilen);

        if (clientFd >= 0) {
            int idx = nextWorker.fetch_add(1) % numWorkers;
            EventLoop *workerLoop = workerLoops[idx];
            std::cout << "New connection fd=" << clientFd << " -> worker" << idx << std::endl;

            workerLoop->runInLoop([workerLoop, clientFd]() {
                auto clientChannel = std::make_shared<Channel>(clientFd, workerLoop);
                workerLoop->holdChannel(clientChannel);
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
                                send(ch->fd(), responseStr.data(), responseStr.size(), 0);
                                shutdown(ch->fd(), SHUT_WR);
                            }
                        }
                    } else {
                        ch->handleClose();
                    }
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
