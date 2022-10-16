#include <cstring>
#include <cstdlib>
#include <utility>
#include "http_server.hpp"
#include "logger.hpp"
#include "json.h"
#include "libbase64.h"

using namespace SHS1;
using namespace SNL1;

struct StringResponse : public ResponseBody {
    std::string s;
    bool consumed;

    explicit StringResponse(std::string s) : s(std::move(s)), consumed{false} {}

    std::pair<const char *, size_t> get() override {
        if (consumed)return {nullptr, 0};
        consumed = true;
        return {s.data(), s.size()};
    }

    ssize_t len() override {
        return (ssize_t) s.size();
    }

};

struct EchoHandler {
    Json::Value rd;
    std::string bs;
    bool hasBody;
    base64_state b64s;

    EchoHandler() : rd{}, bs{}, hasBody{}, b64s{} {}

    void operator()(HttpHeader *header, HttpData *body, std::unique_ptr<Response> &resp) {
        if (header) {
            header->result = HeaderAction::OK;
            rd = Json::objectValue;
            rd["method"] = header->method;
            rd["target"] = header->target;
            rd["version"] = header->version;
            rd["header"] = Json::objectValue;
            for (auto &&[k, v]: header->header) {
                rd["header"][k] = v;
                if (k == normalizeFieldName("Expect")) {
                    Logger::global->log(LOG_ERROR, "expect header is not supported yet");
                    header->result = HeaderAction::CLOSE;
                }
            }
            hasBody = false;
            bs = "";
        } else if (body) {
            if (!hasBody) {
                hasBody = true;
                base64_stream_encode_init(&b64s, 0);
            }
            size_t len, old = bs.size();
            bs.resize(old + (body->length * 4 / 3 + 4));
            base64_stream_encode(&b64s, body->data, body->length, bs.data() + old, &len);
            bs.resize(old + len);
        } else {
            if (hasBody) {
                size_t len, old = bs.size();
                bs.resize(old + 4);
                base64_stream_encode_final(&b64s, bs.data() + old, &len);
                bs.resize(old + len);
                rd["body"] = bs;
            }
            resp = std::make_unique<Response>();
            resp->version = "1.1";
            resp->status = 200;
            resp->message = "OK";
            resp->header.emplace(normalizeFieldName("Server"), "simple-http-server");
            resp->header.emplace(normalizeFieldName("Content-Type"), "application/json");
            resp->header.emplace(normalizeFieldName("Cache-Control"), "max-age=0");
            resp->body = std::make_unique<StringResponse>(rd.toStyledString());
        }
    }

};

int main() {
    int port = 8080, ec;
    Context::ignorePipeSignal();
    Context::blockIntSignal();

    Context ctx(8, 65536, 65536);
    std::shared_ptr<Listener> listener = ctx.newTcpServer(port, 4096, ec);
    if (!listener) {
        panic(strerror(ec));
    }
    std::shared_ptr<HttpServer> httpServer = HttpServer::create(std::move(listener));
    httpServer->enableHandler([]() { return EchoHandler(); });

    Logger::global->log(LOG_INFO, std::string("HTTP server serving on port ") + std::to_string(port));
    Context::waitUntilInterrupt();
    Logger::global->log(LOG_INFO, "caught SIGINT, exiting...");
    httpServer->stop();

    return 0;
}
