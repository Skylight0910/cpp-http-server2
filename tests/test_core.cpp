#include <iostream>
#include <string>

#include "Buffer.h"
#include "HttpRequest.h"
#include "HttpResponse.h"

namespace {

int failures = 0;

void require(bool condition, const std::string& name) {
    if (!condition) {
        ++failures;
        std::cerr << "FAILED: " << name << '\n';
    }
}

void testBuffer() {
    Buffer buffer;
    const std::string request = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    buffer.append(request.data(), request.size());

    require(buffer.size() == request.size(), "buffer stores appended data");
    require(buffer.findCRLF() == 14, "buffer finds first CRLF");
    require(buffer.retrieveAsString(3) == "GET", "buffer retrieves prefix");
    require(buffer.size() == request.size() - 3, "buffer removes retrieved prefix");
    buffer.clear();
    require(buffer.empty(), "buffer clear removes all data");
}

void testHttpRequest() {
    HttpRequest request;
    const std::string valid =
        "GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: test\r\n\r\n";
    require(request.parse(valid), "valid request parses");
    require(request.method() == "GET", "request method is parsed");
    require(request.path() == "/", "request path is parsed");
    require(request.version() == "HTTP/1.1", "request version is parsed");
    require(request.getHeader("Host") == "example.com", "request header is parsed");
    require(request.isDone(), "request reaches done state");

    request.reset();
    require(!request.parse("GET /\r\n\r\n"), "incomplete request line is rejected");

    request.reset();
    require(!request.parse("GET / HTTP/1.1\r\nInvalid Header\r\n\r\n"),
            "malformed header is rejected");

    request.reset();
    require(!request.parse("GET example.com HTTP/1.1\r\nHost: a\r\n\r\n"),
            "invalid request path is rejected");

    request.reset();
    require(!request.parse("GET / FTP/1.1\r\nHost: a\r\n\r\n"),
            "invalid protocol version is rejected");

    request.reset();
    require(request.parse("POST / HTTP/1.1\r\nHost: a\r\n\r\n"),
            "unsupported but syntactically valid method parses");

    request.reset();
    require(request.parse("GET / HTTP/1.1\r\nHost: a\r\n\r\n") &&
            request.shouldKeepAlive(),
            "HTTP/1.1 defaults to keep-alive");

    request.reset();
    require(request.parse("GET / HTTP/1.1\r\nHost: a\r\nConnection: close\r\n\r\n") &&
            !request.shouldKeepAlive(),
            "HTTP/1.1 honors Connection close");

    request.reset();
    require(request.parse("GET / HTTP/1.0\r\nHost: a\r\nConnection: keep-alive\r\n\r\n") &&
            request.shouldKeepAlive(),
            "HTTP/1.0 honors explicit keep-alive");
}

void testHttpResponse() {
    HttpResponse response;
    response.setStatus(404, "Not Found");
    response.addHeader("Content-Type", "text/plain");
    response.setBody("missing");

    const std::string expected =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 7\r\n"
        "Connection: close\r\n"
        "\r\n"
        "missing";
    require(response.toString() == expected, "response is serialized correctly");

    response.setConnection("keep-alive");
    require(response.toString().find("Connection: keep-alive\r\n") !=
            std::string::npos,
            "response supports keep-alive connection header");
}

} // namespace

int main() {
    testBuffer();
    testHttpRequest();
    testHttpResponse();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All core tests passed\n";
    return 0;
}
