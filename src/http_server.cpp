#include "http_server.hpp"
#include "llhttp.h"
#include "tcp_socket.hpp"
#include "logger.hpp"
#include <string>
#include <unordered_map>
#include <queue>
#include <cstring>

namespace SHS1 {

namespace {
using namespace SNL1;
}

ResponseBody::~ResponseBody() = default;

enum class ResponseState {
    NEW, HEADER, BODY
};

struct PendingResponse {
    std::unique_ptr<Response> resp;
    ResponseState state;
    std::string hs;
    std::unique_ptr<char[]> buf;
    size_t cur, size;

    explicit PendingResponse(std::unique_ptr<Response> resp) :
            resp(std::move(resp)),
            state(ResponseState::NEW),
            hs{}, buf{},
            cur(0), size(0) {}
};


HttpHeader::HttpHeader(const std::string &method, const std::string &target, const std::string &version,
                       const std::unordered_map<std::string, std::string> &header) :
        method(method), target(target), version(version), header(header),
        result(HeaderAction::OK) {}


class HttpStreamImpl final : public std::enable_shared_from_this<HttpStreamImpl> {
public:
    explicit HttpStreamImpl(std::shared_ptr<Connection> conn, size_t bufSize) :
            parser_{}, settings_{},
            finish_(false), skip_(false), keepalive_(true),
            b_(std::make_unique<char[]>(bufSize)), bs_(bufSize),
            conn_(std::move(conn)) {
        settings_.on_message_begin = onMessageBegin;
        settings_.on_method = onMethod;
        settings_.on_version = onVersion;
        settings_.on_url = onUrl;
        settings_.on_header_field = onHeaderField;
        settings_.on_header_value = onHeaderValue;
        settings_.on_headers_complete = onHeadersComplete;
        settings_.on_body = onBody;
        settings_.on_message_complete = onMessageComplete;
        llhttp_init(&parser_, HTTP_REQUEST, &settings_);
        parser_.data = this;
    }

    void enableHandler(RequestHandler requestHandler) {
        requestHandler_ = std::move(requestHandler);
        conn_->enableHandler([ptr = shared_from_this()](EventType e) {
            ptr->handler_(e);
        }, true, false);
    }

private:
    llhttp_t parser_;
    llhttp_settings_t settings_;
    std::string chf_;
    bool finish_, skip_, keepalive_;
    std::unique_ptr<char[]> b_;
    size_t bs_;
    std::shared_ptr<Connection> conn_;
    RequestHandler requestHandler_;

    std::string method_, target_, version_;
    std::unordered_map<std::string, std::string> header_;
    std::queue<std::unique_ptr<PendingResponse>> resp_;

    void handler_(EventType e) {
        bool httpError = false;
        if (!resp_.empty()) {
            PendingResponse *r = resp_.front().get();
            int ec;
            size_t n;
            switch (r->state) {
                case ResponseState::NEW:
                    r->hs.append("HTTP/").append(r->resp->version).append(" ");
                    r->hs.append(std::to_string(r->resp->status)).append(" ");
                    r->hs.append(r->resp->message).append("\r\n");
                    r->hs.append("Connection: ").append(keepalive_ ? "keep-alive" : "close").append("\r\n");
                    if (r->resp->body) {
                        size_t len = r->resp->body->len();
                        r->hs.append("Content-Length: ").append(std::to_string(len)).append("\r\n");
                    }
                    for (auto &&[k, v]: r->resp->header) {
                        r->hs.append(k).append(": ").append(v).append("\r\n");
                    }
                    r->hs.append("\r\n");
                    r->cur = 0;
                    r->size = r->hs.size();
                    r->state = ResponseState::HEADER;
                    [[fallthrough]];
                case ResponseState::HEADER:
                    if (!(e & EVENT_OUT)) break;
                    n = conn_->hWrite(r->hs.data() + r->cur, r->size - r->cur, ec);
                    if (n > 0) {
                        if ((r->cur += n) == r->size) {
                            r->cur = 0;
                            if (method_ == "HEAD") {
                                r->size = 0;
                                r->buf = nullptr;
                            } else {
                                auto [p, s] = r->resp->body->get();
                                r->size = s;
                                r->buf = std::move(p);
                            }
                            r->state = ResponseState::BODY;
                        } else break;
                    } else {
                        if (ec) {
                            Logger::global->log(LOG_WARN, strerror(ec));
                            conn_->hShutdown(true, true);
                        }
                        break;
                    }
                    [[fallthrough]];
                case ResponseState::BODY:
                    if (!(e & EVENT_OUT)) break;
                    while (r->buf) {
                        n = conn_->hWrite(r->buf.get() + r->cur, r->size - r->cur, ec);
                        if (n > 0) {
                            if ((r->cur += n) == r->size) {
                                auto [p, s] = r->resp->body->get();
                                r->cur = 0;
                                r->size = s;
                                r->buf = std::move(p);
                            } else break;
                        } else {
                            if (ec) {
                                Logger::global->log(LOG_WARN, strerror(ec));
                                conn_->hShutdown(true, true);
                            }
                            break;
                        }
                    }
                    if (!r->buf) {
                        resp_.pop();
                    }
            }
        } else {
            if ((e & EVENT_IN) && !skip_) {
                int ec;
                size_t n;
                do {
                    n = conn_->hRead(b_.get(), bs_, ec);
                    if (n > 0) {
                        llhttp_errno_t err;
                        if ((err = llhttp_execute(&parser_, b_.get(), n)) != HPE_OK) {
                            Logger::global->log(LOG_WARN, std::string("http err: ") + llhttp_errno_name(err));
                            httpError = true;
                        }
                    } else {
                        if (ec) {
                            Logger::global->log(LOG_WARN, strerror(ec));
                            conn_->hShutdown(true, true);
                        }
                    }
                } while (n == bs_);
            }
            if (conn_->hIsReadClosed() && !finish_ && !skip_) {
                finish_ = true;
                llhttp_errno_t err;
                if ((err = llhttp_finish(&parser_)) != HPE_OK) {
                    Logger::global->log(LOG_WARN, std::string("http err: ") + llhttp_errno_name(err));
                    httpError = true;
                }
            }
        }

        conn_->hSetWrite(!resp_.empty());
        conn_->hSetRead(resp_.empty() && !skip_);
        if (skip_) {
            conn_->hShutdown(true, false);
        }
        if (httpError || conn_->hIsWriteClosed() || (conn_->hIsReadClosed() && resp_.empty())) {
            conn_->hShutdown(true, true);
        }
    }

    static int onMessageBegin(llhttp_t *parser) {
        auto o = (HttpStreamImpl *) parser->data;
        o->method_ = o->target_ = o->version_ = "";
        o->header_.clear();
        return 0;
    }

    static int onMethod(llhttp_t *parser, const char *at, size_t length) {
        auto o = (HttpStreamImpl *) parser->data;
        o->method_ = std::string(at, at + length);
        return 0;
    }

    static int onVersion(llhttp_t *parser, const char *at, size_t length) {
        auto o = (HttpStreamImpl *) parser->data;
        o->version_ = std::string(at, at + length);
        return 0;
    }

    static int onUrl(llhttp_t *parser, const char *at, size_t length) {
        auto o = (HttpStreamImpl *) parser->data;
        o->target_ = std::string(at, at + length);
        return 0;
    }

    static int onHeaderField(llhttp_t *parser, const char *at, size_t length) {
        auto o = (HttpStreamImpl *) parser->data;
        o->chf_ = normalizeFieldName(at, length);
        return 0;
    }

    static int onHeaderValue(llhttp_t *parser, const char *at, size_t length) {
        auto o = (HttpStreamImpl *) parser->data;
        std::string value(at, at + length);
        auto [it, succ] = o->header_.emplace(o->chf_, value);
        if (!succ) {  // combine same header to comma separated list
            it->second.push_back(',');
            it->second.append(value);
        }
        return 0;
    }

    static int onHeadersComplete(llhttp_t *parser) {
        auto o = (HttpStreamImpl *) parser->data;
        HttpHeader header{o->method_, o->target_, o->version_, o->header_};
        std::unique_ptr<Response> response;
        o->requestHandler_(&header, nullptr, response);
        if (header.result == HeaderAction::SKIP_BODY) {
            if (!response)return -1;
            o->resp_.push(std::make_unique<PendingResponse>(std::move(response)));
            o->skip_ = true;
        }
        return header.result == HeaderAction::CLOSE ? -1 : 0;
    }

    static int onBody(llhttp_t *parser, const char *at, size_t length) {
        auto o = (HttpStreamImpl *) parser->data;
        std::unique_ptr<Response> response;
        HttpData data{at, length};
        o->requestHandler_(nullptr, &data, response);
        // ignoring supplied response
        return 0;
    }

    static int onMessageComplete(llhttp_t *parser) {
        auto o = (HttpStreamImpl *) parser->data;
        std::unique_ptr<Response> response;
        o->keepalive_ = llhttp_should_keep_alive(parser);
        o->requestHandler_(nullptr, nullptr, response);
        if (response) {
            o->resp_.push(std::make_unique<PendingResponse>(std::move(response)));
            return 0;
        }
        // no response means to forcibly close connection
        return -1;
    }
};


HttpServer::HttpServer(std::shared_ptr<Listener> lis) :
        listener_(std::move(lis)),
        rbs_(DEFAULT_READ_BUFFER_SIZE) {}

void HttpServer::enableHandler(NewClientHandler h) {
    newClientHandler_ = std::move(h);
    listener_->enableHandler([ptr = shared_from_this()](EventType e) {
        ptr->acceptHandler_(e);
    });
}

void HttpServer::configureReadBufferSize(size_t s) {
    rbs_ = s;
}

void HttpServer::stop() {
    listener_->stop();
}

void HttpServer::acceptHandler_(EventType e) {
    if (e & EVENT_IN) {
        for (;;) {
            int ec;
            std::shared_ptr<Connection> c = listener_->hAccept(ec);
            if (c) {
                std::shared_ptr<HttpStreamImpl> hs = std::make_shared<HttpStreamImpl>(std::move(c), rbs_);
                hs->enableHandler(newClientHandler_());
            } else {
                if (ec) {
                    Logger::global->log(LOG_WARN, strerror(ec));
                    if (ec == ENFILE || ec == EMFILE) {
                        listener_->hShutdown();
                    }
                }
                break;
            }
        }
    }
}

std::shared_ptr<HttpServer> HttpServer::create(std::shared_ptr<Listener> lis) {
    return std::shared_ptr<HttpServer>(new HttpServer(std::move(lis)));
}

}