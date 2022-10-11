#include <cstring>
#include <cstdlib>
#include "http_server.hpp"
#include "logger.hpp"
#include "json.h"

using namespace SHS1;
using namespace SNL1;

struct StringResponse : public ResponseBody {
    std::string s;

    explicit StringResponse(const std::string &s) : s(s) {}

    std::pair<std::unique_ptr<char[]>, size_t> get() override {
        if (!s.empty()) {
            size_t n = s.size();
            std::unique_ptr<char[]> b = std::make_unique<char[]>(n);
            memcpy(b.get(), s.data(), n);
            s = {};
            return {std::move(b), n};
        }
        return {nullptr, 0};
    }

    ssize_t len() override {
        return (ssize_t) s.size();
    }

};

struct EchoHandler {
    Json::Value rd;

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
        } else if (body) {
        } else {
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
    std::shared_ptr<Listener> listener = ctx.newTcpServer(port, 128, ec);
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
