#include <cstring>
#include "http_server.hpp"
#include "logger.hpp"

using namespace SHS1;
using namespace SNL1;

struct EchoHandler {

    void operator()(HttpHeader *header, HttpData *body, std::unique_ptr<Response> &resp) {
        if (header) {

        } else if (body) {

        } else {

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
