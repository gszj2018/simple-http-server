#ifndef SIMPLE_HTTP_SERVER_HTTP_SERVER_HPP
#define SIMPLE_HTTP_SERVER_HTTP_SERVER_HPP

#include "io_context.hpp"
#include<functional>
#include<string>
#include<memory>
#include<unordered_map>
#include<variant>
#include<algorithm>
#include<cctype>

namespace SHS1 {

namespace {
using SNL1::CtxObject;
using SNL1::Context;
using SNL1::Listener;
using SNL1::EventType;
}

enum class HeaderAction {
    OK,             // continue to process the request normally
    SKIP_BODY,      // instantly respond to client, then close connection
    CLOSE           // instantly close connection
};

inline std::string normalizeFieldName(const char *at, size_t len) {
    std::string s(len, ' ');
    std::transform(at, at + len, s.begin(), [init = true](char c)mutable {
        char r = static_cast<char>(init ? std::toupper(c) : std::tolower(c));
        init = c == '-';
        return r;
    });
    return s;
}

inline std::string normalizeFieldName(const std::string &s) {
    return normalizeFieldName(s.data(), s.length());
}

struct HttpHeader {
    const std::string &method;
    const std::string &target;
    const std::string &version;
    const std::unordered_map<std::string, std::string> &header;
    HeaderAction result;

    HttpHeader(const std::string &method, const std::string &target, const std::string &version,
               const std::unordered_map<std::string, std::string> &header);
};

struct HttpData {
    const char *data;
    size_t length;
};

class ResponseBody {
public:

    // returning nullptr indicate EOF
    // returning char[0] DOES NOT indicate EOF
    virtual std::pair<std::unique_ptr<char[]>, size_t> get() = 0;

    virtual ssize_t len() = 0;

    virtual ~ResponseBody();

    // currently we don't support chunked transfer
    // len() should return body length, even before actual data transfer
    // static constexpr ssize_t CHUNKED = -1;

};

struct Response {
    std::string version;
    int status;
    std::string message;
    std::unordered_map<std::string, std::string> header;
    std::unique_ptr<ResponseBody> body;
};

//  HttpHeader*  HttpData*   event
//  non-null     null        header ready
//  null         non-null    body part arrived
//  null         null        message done
using RequestHandler = std::function<void(HttpHeader *, HttpData *, std::unique_ptr<Response> &)>;

using NewClientHandler = std::function<RequestHandler()>;

class HttpServer final : public std::enable_shared_from_this<HttpServer> {
public:
    static constexpr size_t DEFAULT_READ_BUFFER_SIZE = 1048576;

    void enableHandler(NewClientHandler h);

    void configureReadBufferSize(size_t s);

    void stop();

    static std::shared_ptr<HttpServer> create(std::shared_ptr<Listener> lis);

private:
    std::shared_ptr<Listener> listener_;
    NewClientHandler newClientHandler_;
    size_t rbs_;

    explicit HttpServer(std::shared_ptr<Listener> lis);

    void acceptHandler_(EventType e);
};

}

#endif //SIMPLE_HTTP_SERVER_HTTP_SERVER_HPP
