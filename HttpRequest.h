#pragma once
#include <string>
#include <map>

class HttpRequest {
public:
    enum ParseState {
        kRequestLine,
        kHeaders,
        kBody,
        kDone
    };

    HttpRequest();

    bool parse(const std::string& data);
    bool isDone() const { return state_ == kDone; }

    const std::string& method() const { return method_; }
    const std::string& path() const { return path_; }
    const std::string& version() const { return version_; }
    const std::string& getHeader(const std::string& key) const;

    void reset();

private:
    bool parseRequestLine(const std::string& line);
    bool parseHeader(const std::string& line);

    ParseState state_;
    std::string method_;
    std::string path_;
    std::string version_;
    std::map<std::string, std::string> headers_;
};
