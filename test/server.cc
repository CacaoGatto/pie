#include "pie.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>

char server_string_1[] = "Hello, Client!\n";
char server_string_2[] = "I'm the Server\n";

enum ServerStage {
    kServerStageRecvHead = 0,
    kServerStageSendHead,
    kServerStageRecvTail,
    kServerStageSendTail,
};

struct ServerContext {
    char buffer[32] = {0};
    uint32_t stage = kServerStageRecvHead;
};

void init_handler(void *udata, PieSocket::EpollUserContext *ctx) {
    (void)udata;
    ServerContext *sc = new ServerContext;
    ctx->buffer.SetAddr(sc->buffer).SetSize(32).SetRecv();
    ctx->context = sc;
}

void send_handler(void *udata, PieSocket::EpollUserContext *ctx) {
    (void)udata;
    ServerContext *sc = (ServerContext *)ctx->context;
    switch (sc->stage) {
    case kServerStageSendHead:
        ctx->buffer.SetAddr(sc->buffer).SetSize(32).SetRecv();
        break;
    case kServerStageSendTail:
        ctx->buffer.SetComp();
        break;
    default:
        break;
    }
    sc->stage += 1;
}

void recv_handler(void *udata, PieSocket::EpollUserContext *ctx) {
    (void)udata;
    ServerContext *sc = (ServerContext *)ctx->context;
    switch (sc->stage) {
    case kServerStageRecvHead:
        printf("%s", sc->buffer);
        ctx->buffer.SetAddr(server_string_1).SetSize(16).SetSend();
        break;
    case kServerStageRecvTail:
        printf("%s", sc->buffer);
        ctx->buffer.SetAddr(server_string_2).SetSize(16).SetSend();
        break;
    default:
        break;
    }
    sc->stage += 1;
}

void comp_handler(void *udata, PieSocket::EpollUserContext *ctx) {
    (void)udata;
    ServerContext *sc = (ServerContext *)ctx->context;
    delete sc;
}

int main() {
    char *ip = getenv("TEST_PIE_SERVER_IP");
    int conn = atoi(getenv("TEST_PIE_CONN_NUM"));
    printf ("%d\n", conn);
    PieSocket socket;
    socket.Listen(conn, ip);
    socket.RegisterInit(init_handler)
          .RegisterSend(send_handler)
          .RegisterRecv(recv_handler)
          .RegisterComp(comp_handler)
          .RegisterUdata(nullptr)
          .EpollLaunch();
    while (conn > 0) {
        int ret = socket.EpollRetrive();
        if (ret > 0) {
            conn -= ret;
        }
    }
    socket.StopListen();
    return 0;
}