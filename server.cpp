#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
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

struct ServerConfig {
    int port = 8080;
    int workers = 1;
    int64_t idleTimeoutMs = 5000;
    size_t maxHeaderSize = 8 * 1024;
};

void printUsage(const char* program) {
    std::cout
        << "Usage: " << program << " [options]\n"
        << "  -p, --port PORT             Listen port (default: 8080)\n"
        << "  -w, --workers N             Worker loop count (default: 1)\n"
        << "      --idle-timeout-ms MS    Idle connection timeout (default: 5000)\n"
        << "      --max-header-size BYTES Request header limit (default: 8192)\n"
        << "  -h, --help                  Show this help\n";
}

bool parseInt(const char* value, int& result) {
    try {
        size_t parsed = 0;
        const int parsedValue = std::stoi(value, &parsed);
        if (parsed != std::string(value).size()) return false;
        result = parsedValue;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseArguments(int argc, char* argv[], ServerConfig& config) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        auto requireValue = [&](const std::string& name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << '\n';
                return nullptr;
            }
            return argv[++i];
        };

        if (argument == "-p" || argument == "--port") {
            const char* value = requireValue(argument);
            if (!value || !parseInt(value, config.port) ||
                config.port < 1 || config.port > 65535) {
                std::cerr << "Invalid port: " << (value ? value : "") << '\n';
                return false;
            }
        } else if (argument == "-w" || argument == "--workers") {
            const char* value = requireValue(argument);
            if (!value || !parseInt(value, config.workers) ||
                config.workers < 1 || config.workers > 256) {
                std::cerr << "Invalid worker count: " << (value ? value : "") << '\n';
                return false;
            }
        } else if (argument == "--idle-timeout-ms") {
            const char* value = requireValue(argument);
            int timeout = 0;
            if (!value || !parseInt(value, timeout) || timeout < 1) {
                std::cerr << "Invalid idle timeout: " << (value ? value : "") << '\n';
                return false;
            }
            config.idleTimeoutMs = timeout;
        } else if (argument == "--max-header-size") {
            const char* value = requireValue(argument);
            int size = 0;
            if (!value || !parseInt(value, size) || size < 128) {
                std::cerr << "Invalid max header size: " << (value ? value : "") << '\n';
                return false;
            }
            config.maxHeaderSize = static_cast<size_t>(size);
        } else if (argument == "-h" || argument == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            printUsage(argv[0]);
            return false;
        }
    }
    return true;
}

bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        std::cerr << "fcntl(F_GETFL) failed: " << strerror(errno) << '\n';
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cerr << "fcntl(F_SETFL) failed: " << strerror(errno) << '\n';
        return false;
    }
    return true;
}

void refreshIdleTimer(EventLoop* loop,
                      const std::weak_ptr<Channel>& weakChannel,
                      const ServerConfig& config) {
    auto channel = weakChannel.lock();
    if (!channel) return;

    loop->addTimer(channel->fd(), EventLoop::nowMs() + config.idleTimeoutMs,
                   [weakChannel]() {
        auto channel = weakChannel.lock();
        if (!channel) return;

        std::cout << "[TIMER] idle connection timeout fd="
                  << channel->fd() << '\n';
        channel->handleClose();
    });
}

HttpResponse makeTextResponse(int status,
                              const std::string& message,
                              const std::string& body) {
    HttpResponse response;
    response.setStatus(status, message);
    response.addHeader("Content-Type", "text/html; charset=utf-8");
    response.setBody(body);
    return response;
}

bool canReuseConnection(const HttpRequest& request) {
    if (!request.shouldKeepAlive()) return false;
    if (request.method() != "GET") return false;

    const std::string& contentLength = request.getHeader("Content-Length");
    return contentLength.empty() || contentLength == "0";
}

int createListenSocket(const ServerConfig& config) {
    const int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        std::cerr << "socket() failed: " << strerror(errno) << '\n';
        return -1;
    }

    if (!setNonBlocking(listenFd)) {
        close(listenFd);
        return -1;
    }

    int reuseAddress = 1;
    if (setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR,
                   &reuseAddress, sizeof(reuseAddress)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed: "
                  << strerror(errno) << '\n';
        close(listenFd);
        return -1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(config.port));
    address.sin_addr.s_addr = INADDR_ANY;
    if (bind(listenFd, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) < 0) {
        std::cerr << "bind() failed on port " << config.port
                  << ": " << strerror(errno) << '\n';
        close(listenFd);
        return -1;
    }

    if (listen(listenFd, SOMAXCONN) < 0) {
        std::cerr << "listen() failed: " << strerror(errno) << '\n';
        close(listenFd);
        return -1;
    }
    return listenFd;
}

} // namespace

int main(int argc, char* argv[]) {
    ServerConfig config;
    if (!parseArguments(argc, argv, config)) {
        return 1;
    }

    const int listenFd = createListenSocket(config);
    if (listenFd < 0) {
        return 1;
    }

    std::cout << "Server started on port " << config.port
              << " with " << config.workers << " worker(s)" << std::endl;

    EventLoop mainLoop;
    const int numWorkers = config.workers;
    std::vector<std::unique_ptr<EventLoopThread>> threads;
    std::vector<EventLoop*> workerLoops;

    for (int i = 0; i < numWorkers; ++i) {
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

            if (!setNonBlocking(clientFd)) {
                close(clientFd);
                continue;
            }

            int idx = nextWorker.fetch_add(1) % numWorkers;
            EventLoop *workerLoop = workerLoops[idx];
            std::cout << "New connection fd=" << clientFd << " -> worker" << idx << std::endl;

            workerLoop->runInLoop([workerLoop, clientFd, config]() {
                auto clientChannel = std::make_shared<Channel>(clientFd, workerLoop);
                workerLoop->holdChannel(clientChannel);
                // Channel 生命周期保护：回调链触发关闭时，保证本次事件处理完整结束
                clientChannel->tie(clientChannel);
                std::weak_ptr<Channel> weakChannel = clientChannel;

                clientChannel->setReadCallback([weakChannel, workerLoop, config]() {
                    auto channel = weakChannel.lock();
                    if (!channel) return;

                    const ssize_t n =
                        channel->inputBuffer().readFromFd(channel->fd());

                    if (n > 0) {
                        refreshIdleTimer(workerLoop, weakChannel, config);

                        if (channel->inputBuffer().size() >
                            config.maxHeaderSize) {
                            // 请求头总量超过上限：无论是否完整，都拒绝并断连
                            std::cerr << "[GUARD] header too large fd="
                                      << channel->fd() << " size="
                                      << channel->inputBuffer().size() << '\n';
                            HttpResponse response = makeTextResponse(
                                431, "Request Header Fields Too Large",
                                "<html><body><h1>431</h1></body></html>");
                            channel->reject(response.toString());
                            return;
                        }

                        while (true) {
                            const char* end =
                                strstr(channel->inputBuffer().data(), "\r\n\r\n");
                            if (!end) break;

                            size_t requestLen =
                                end - channel->inputBuffer().data() + 4;
                            std::string rawRequest =
                                channel->inputBuffer().retrieveAsString(requestLen);

                            HttpRequest request;
                            if (!request.parse(rawRequest)) {
                                std::cerr << "[PARSE] bad request fd="
                                          << channel->fd() << '\n';
                                HttpResponse response = makeTextResponse(
                                    400, "Bad Request",
                                    "<html><body><h1>400</h1></body></html>");
                                channel->reject(response.toString());
                                break;
                            }

                            const bool keepAlive = canReuseConnection(request);

                            std::cout << "Request: " << request.method() << " "
                                      << request.path() << std::endl;

                            HttpResponse response;
                            response.setConnection(
                                keepAlive ? "keep-alive" : "close");

                            if (request.method() != "GET") {
                                response.setStatus(405, "Method Not Allowed");
                                response.addHeader("Allow", "GET");
                                response.setBody(
                                    "<html><body><h1>405</h1></body></html>");
                            } else if (request.path() == "/") {
                                response.setStatus(200, "OK");
                                response.addHeader(
                                    "Content-Type", "text/html; charset=utf-8");
                                response.setBody(
                                    "<html><body><h1>Hello!</h1></body></html>");
                            } else {
                                response.setStatus(404, "Not Found");
                                response.addHeader(
                                    "Content-Type", "text/html; charset=utf-8");
                                response.setBody(
                                    "<html><body><h1>404</h1></body></html>");
                            }

                            channel->setCloseAfterFlush(!keepAlive);
                            channel->sendData(response.toString());

                            if (!keepAlive && channel->outputBuffer().empty()) {
                                channel->handleClose();
                                break;
                            }
                        }
                    } else if (n == 0) {
                        channel->handleClose();
                    } else {
                        // n < 0：EAGAIN 不是错误；真实错误才断连
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // LT 模式下一般不会走到这里，防御性保留连接
                        } else {
                            std::cerr << "[READ] error fd=" << channel->fd()
                                      << " errno=" << strerror(errno) << std::endl;
                            channel->handleClose();
                        }
                    }
                });

                clientChannel->setWriteCallback([weakChannel]() {
                    auto channel = weakChannel.lock();
                    if (!channel) return;
                    channel->flushOutput();
                });

                clientChannel->setCloseCallback([workerLoop, weakChannel]() {
                    workerLoop->runInLoop([workerLoop, weakChannel]() {
                        auto channel = weakChannel.lock();
                        if (!channel) return;

                        std::cout << "[CLOSE] remove fd="
                                  << channel->fd() << '\n';
                        workerLoop->removeChannel(channel.get());
                    });
                });

                refreshIdleTimer(workerLoop, weakChannel, config);
                clientChannel->enableReading();
            });
        }
    });

    listenChannel.enableReading();
    mainLoop.loop();

    close(listenFd);
    return 0;
}
