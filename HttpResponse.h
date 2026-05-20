#pragma once
#include <string>

class HttpResponse {
public:
    HttpResponse();

    void setStatus(int code, const std::string& message);
    void addHeader(const std::string& key, const std::string& value);
    void setBody(const std::string& body);
    std::string toString() const;

private:
    int statusCode_;
    std::string statusMessage_;
    std::string headers_;
    std::string body_;
};
