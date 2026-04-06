#include "pie.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <pthread.h>

char client_string_1[] = "Hello, Server!\n";
char client_string_2[] = "I'm the Client\n";

void *client(void *ip) {
    char buffer[32];
    uint64_t size = 32;
    pie::PieSocket socket;
    int fd = socket.ConnectServer((char *)ip);
    socket.SendBuf(fd, client_string_1, sizeof(client_string_1));
    socket.RecvBuf(fd, buffer, &size);
    printf("%s", buffer);
    size = 32;
    socket.SendBuf(fd, client_string_2, sizeof(client_string_2));
    socket.RecvBuf(fd, buffer, &size);
    printf("%s", buffer);
    socket.DisconnectServer(fd);
    return nullptr;
}

int main() {
    char *ip = getenv("TEST_PIE_SERVER_IP");
    int conn = atoi(getenv("TEST_PIE_CONN_NUM"));
    pthread_t threads[conn];
    for (int i = 0; i < conn; i++) {
        pthread_create(&threads[i], nullptr, client, ip);
    }
    for (int i = 0; i < conn; i++) {
        pthread_join(threads[i], nullptr);
    }
    return 0;
}
