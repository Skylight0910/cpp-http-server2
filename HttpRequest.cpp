#include "HttpRequest.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return value;
}

bool equalsIgnoreCase(const std::string& lhs, const std::string& rhs) {
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                      [](unsigned char a, unsigned char b) {
                          return std::tolower(a) == std::tolower(b);
                      });
}

} // namespace

static std::vector<std::string> split(const std::string& str) {
    std::vector<std::string> result;
    std::istringstream iss(str);
    std::string word;
    while (iss >> word) {
        result.push_back(word);
    }
    return result;
}

HttpRequest::HttpRequest() : state_(kRequestLine) {}

bool HttpRequest::parseRequestLine(const std::string& line) {
    auto parts = split(line);
    if (parts.size() != 3) return false;
    if (parts[0].empty() || parts[1].empty() || parts[1][0] != '/' ||
        parts[2].rfind("HTTP/", 0) != 0) {
        return false;
    }
    method_ = parts[0];
    path_ = parts[1];
    version_ = parts[2];
    state_ = kHeaders;
    return true;
}

bool HttpRequest::parseHeader(const std::string& line) {
    if (line.empty()) {
        state_ = kDone;
        return true;
    }
    size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0) return false;
    std::string key = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    if (!value.empty() && value[0] == ' ') {
        value.erase(0, 1);
    }
    headers_[toLower(key)] = value;
    return true;
}

bool HttpRequest::parse(const std::string& data) {
    std::istringstream stream(data);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        switch (state_) {
            case kRequestLine:
                if (!parseRequestLine(line)) return false;
                break;
            case kHeaders:
                if (!parseHeader(line)) return false;
                if (state_ == kDone) return true;
                break;
            case kBody:
            case kDone:
                break;
        }
    }
    return false;
}

const std::string& HttpRequest::getHeader(const std::string& key) const {
    static const std::string empty;
    auto it = headers_.find(toLower(key));
    if (it != headers_.end()) return it->second;
    return empty;
}

bool HttpRequest::shouldKeepAlive() const {
    const std::string& connection = getHeader("Connection");

    if (version_ == "HTTP/1.1") {
        return !equalsIgnoreCase(connection, "close");
    }

    if (version_ == "HTTP/1.0") {
        return equalsIgnoreCase(connection, "keep-alive");
    }

    return false;
}

void HttpRequest::reset() {
    state_ = kRequestLine;
    method_.clear();
    path_.clear();
    version_.clear();
    headers_.clear();
}
