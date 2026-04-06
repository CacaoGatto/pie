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

void *init_handler(void *sock_ctx, void *task_ctx, pie::EpollBuffer &buffer) {
    (void)sock_ctx;
    ServerContext *sc = new ServerContext;
    buffer.SetAddr(sc->buffer).SetSize(32).SetRecv();
    return sc;
}

void *send_handler(void *sock_ctx, void *task_ctx, pie::EpollBuffer &buffer) {
    (void)sock_ctx;
    ServerContext *sc = (ServerContext *)task_ctx;
    switch (sc->stage) {
    case kServerStageSendHead:
        buffer.SetAddr(sc->buffer).SetSize(32).SetRecv();
        break;
    case kServerStageSendTail:
        buffer.SetComp();
        break;
    default:
        break;
    }
    sc->stage += 1;
    return sc;
}

void *recv_handler(void *sock_ctx, void *task_ctx, pie::EpollBuffer &buffer) {
    (void)sock_ctx;
    ServerContext *sc = (ServerContext *)task_ctx;
    switch (sc->stage) {
    case kServerStageRecvHead:
        printf("%s", sc->buffer);
        buffer.SetAddr(server_string_1).SetSize(16).SetSend();
        break;
    case kServerStageRecvTail:
        printf("%s", sc->buffer);
        buffer.SetAddr(server_string_2).SetSize(16).SetSend();
        break;
    default:
        break;
    }
    sc->stage += 1;
    return sc;
}

void *comp_handler(void *sock_ctx, void *task_ctx, pie::EpollBuffer &buffer) {
    (void)sock_ctx;
    ServerContext *sc = (ServerContext *)task_ctx;
    delete sc;
    return nullptr;
}

int main() {
    char *ip = getenv("TEST_PIE_SERVER_IP");
    int conn = atoi(getenv("TEST_PIE_CONN_NUM"));
    printf ("%d\n", conn);
    pie::PieSocket socket;
    socket.Listen(conn, ip);
    socket.RegisterInit(init_handler)
          .RegisterSend(send_handler)
          .RegisterRecv(recv_handler)
          .RegisterComp(comp_handler)
          .RegisterContext(nullptr)
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