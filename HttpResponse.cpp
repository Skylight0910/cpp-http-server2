#include "HttpResponse.h"
#include <sstream>

HttpResponse::HttpResponse() : statusCode_(200), statusMessage_("OK") {}

void HttpResponse::setStatus(int code, const std::string& message) {
    statusCode_ = code;
    statusMessage_ = message;
}

void HttpResponse::addHeader(const std::string& key, const std::string& value) {
    headers_ += key + ": " + value + "\r\n";
}

void HttpResponse::setBody(const std::string& body) {
    body_ = body;
}

std::string HttpResponse::toString() const {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << statusCode_ << " " << statusMessage_ << "\r\n";
    oss << headers_;
    oss << "Content-Length: " << body_.size() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << body_;
    return oss.str();
}
