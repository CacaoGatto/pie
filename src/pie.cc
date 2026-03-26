#include "pie.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <sys/epoll.h>

// #define RAW_SOCKET_CONNECT_TRY_ONCE

static inline void print_info(const char * info) {
    printf("[Module Socket] %s\n", info);
    return;
}

static inline void print_error(const char * info) {
    print_info(info);
    printf("[System] %s\n", strerror(errno));
    return;
}

static inline int recv_data(const int sock_fd, void *data, const size_t size) {
    size_t progress = 0;
    while (progress < size) {
        int ret = recv(sock_fd, (char *)data + progress, size - progress, 0);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;  // Retry
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;  // Asynchronous socket
            } else {
                print_error("recv_data failed");
                return -1;
            }
        } else if (ret == 0) {
            print_info("recv_data failed: connection closed");
            return -1;
        }
        progress += ret;
    }
    return 0;
}

static inline int send_data(const int sock_fd, void *data, const size_t size) {
    size_t progress = 0;
    while (progress < size) {
        int ret = send(sock_fd, (char *)data + progress, size - progress, MSG_NOSIGNAL);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;  // Retry
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;  // Asynchronous socket
            } else {
                print_error("send_data failed");
                return -1;
            }
        } else if (ret == 0) {
            print_info("send_data failed: connection closed");
            return -1;
        }
        progress += ret;
    }
    return 0;
}

static int create_tcp_socket(const char *ip_addr, const uint16_t port,
                             sockaddr_in *sock_addr) {
    if (!ip_addr) {
        print_info("Create socket failed: ip_addr is nullptr");
        return -1;
    }
    if (!sock_addr) {
        print_info("Create socket failed: sock_addr is nullptr");
        return -1;
    }
    int ret = 0;
    int on = 1;
    int sock_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        print_error("Create socket failed: socket()");
        return -1;
    }
    ret = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if (ret < 0) {
        print_error("Create socket failed: setsockopt()");
        return -1;
    }
    sock_addr->sin_family = AF_INET;
    sock_addr->sin_port = htons(port);
    sock_addr->sin_addr.s_addr = inet_addr(ip_addr);
    if (sock_addr->sin_addr.s_addr == INADDR_NONE) {
        print_error("Create socket failed: inet_addr()");
        return -1;
    }
    return sock_fd;
}

static int set_tcp_nonblocking(int sock_fd) {
    if (sock_fd < 0) {
        print_info("Set socket nonblocking failed: no available socket");
        return -1;
    }
    int flags = fcntl(sock_fd, F_GETFL, 0);
    if (flags < 0) {
        print_error("Set socket nonblocking failed: GETFL");
        return -1;
    }
    if (fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        print_error("Set socket nonblocking failed: SETFL");
        return -1;
    }
    return 0;
}

PieSocket::PieSocket() {
    ;
}

PieSocket::~PieSocket() {
    ;
}

int PieSocket::ConnectServer(const char *ip_addr, const uint16_t port) {
    int ret = 0;
    sockaddr_in sock_addr;
    memset(&sock_addr, 0, sizeof(sockaddr_in));
    int sock_fd = create_tcp_socket(ip_addr, port, &sock_addr);
    if (sock_fd < 0) {
        return -1;
    }
#ifdef RAW_SOCKET_CONNECT_TRY_ONCE
    ret = connect(sock_fd, (sockaddr *)&sock_addr, sizeof(sockaddr));
    if (ret < 0) {
        print_error("Connect failed: connect()");
        close(sock_fd);
        return -1;
    }
#else
    do {
        ret = connect(sock_fd, (sockaddr *)&sock_addr, sizeof(sockaddr));
    } while (ret < 0);
#endif
    return sock_fd;
}

int PieSocket::DisconnectServer(int sock_fd) {
    if (sock_fd < 0) {
        print_info("Disconnect failed: no available socket");
        return -1;
    }
    if (shutdown(sock_fd, SHUT_WR) < 0) {
        print_error("Disconnect failed: shutdown()");
        return -1;
    }
    char fin[64];
    while (read(sock_fd, fin, 64) > 0) {
        ;  // Ignore garbage data after shutdown
    }
    if (close(sock_fd) < 0) {
        print_error("Disconnect failed: close()");
        return -1;
    }
    return 0;
}

int PieSocket::Listen(int max_conn, const char *ip_addr, const uint16_t port) {
    int ret = 0;
    sockaddr_in sock_addr;
    memset(&sock_addr, 0, sizeof(sockaddr_in));
    int sock_fd = create_tcp_socket(ip_addr, port, &sock_addr);
    if (sock_fd < 0) {
        return -1;
    }
    ret = bind(sock_fd, (sockaddr *)&sock_addr, sizeof(sockaddr));
    if (ret < 0) {
        print_error("Listen failed: bind()");
        close(sock_fd);
        return -1;
    }
    ret = listen(sock_fd, max_conn);
    if (ret < 0) {
        print_error("Listen failed: listen()");
        close(sock_fd);
        return -1;
    }
    listen_fd_ = sock_fd;
    max_conn_ = max_conn;
    return 0;
}

int PieSocket::StopListen() {
    if (listen_fd_ < 0) {
        print_info("StopListen failed: no available socket");
        return -1;
    }
    if (epoll_fd_ >= 0) {
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, listen_fd_, nullptr) < 0) {
            print_error("StopListen failed: epoll_ctl()");
            return -1;
        }
        if (close(epoll_fd_) < 0) {
            print_error("StopListen failed: close(epoll_fd)");
            return -1;
        }
        epoll_fd_ = -1;
    }
    if (close(listen_fd_) < 0) {
        print_error("StopListen failed: close(listen_fd)");
        return -1;
    }
    listen_fd_ = -1;
    return 0;
}

int PieSocket::AcceptClient() {
    if (listen_fd_ < 0) {
        print_info("Accept failed: no available socket");
        return -1;
    }
    int client_fd = accept(listen_fd_, nullptr, nullptr);
    if (client_fd < 0) {
        print_error("Accept failed: accept()");
    }
    return client_fd;
}

int PieSocket::FinishClient(int sock_fd) {
    if (sock_fd < 0) {
        print_info("Finish failed: no available socket");
        return -1;
    }
    char fin[64];
    int ret = read(sock_fd, fin, 64);
    while (ret != 0) {
        if (ret < 0 && errno != EINTR && errno != EWOULDBLOCK) {
            print_error("Finish failed: read()");
            return -1;
        }
        ret = read(sock_fd, fin, 64);
    }
    if (close(sock_fd) < 0) {
        print_error("Finish failed: close()");
        return -1;
    }
    return 0;
}

int PieSocket::SendBuf(int sock_fd, void *buffer, uint64_t size) {
    uint64_t data_volume = size;
    // NOTE: we may need htonll here !
    if (send_data(sock_fd, &data_volume, sizeof(uint64_t))) {
        print_error("Fail to send buffer header");
        return -1;
    }
    if (send_data(sock_fd, buffer, size)) {
        print_error("Fail to send buffer payload");
        return -1;
    }
    return 0;
}

int PieSocket::RecvBuf(int sock_fd, void *buffer, uint64_t *size) {
    uint64_t data_volume = 0;
    if (recv_data(sock_fd, &data_volume, sizeof(uint64_t))) {
        print_error("Fail to recv buffer header");
        return -1;
    }
    if (data_volume > *size) {
        print_error("Not enough recv buffer");
        return -1;
    }
    // NOTE: we may need ntohll here !
    if (recv_data(sock_fd, buffer, data_volume)) {
        print_error("Fail to recv buffer payload");
        return -1;
    }
    *size = data_volume;
    return 0;
}

int PieSocket::EpollLaunch() {
    if (listen_fd_ < 0) {
        print_info("Register epoll failed: no available socket");
        return -1;
    }
    if (set_tcp_nonblocking(listen_fd_) < 0) {
        print_info("Register epoll failed: set_tcp_nonblocking()");
        return -1;
    }
    epoll_fd_ = epoll_create(max_conn_);
    if (epoll_fd_ < 0) {
        print_error("Register epoll failed: epoll_create()");
        return -1;
    }
    epoll_event event;
    memset(&event, 0, sizeof(epoll_event));
    event.data.fd = listen_fd_;
    event.events = EPOLLIN;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event) < 0) {
        print_error("Register epoll failed: epoll_ctl()");
        return -1;
    }
    return 0;
}

int PieSocket::EpollRetrive() {
    if (listen_fd_ < 0) {
        print_info("Epoll failed: no available socket");
        return -1;
    }
    if (epoll_fd_ < 0) {
        print_info("Epoll failed: no available epoll fd");
        return -1;
    }
    epoll_event *events = new epoll_event[max_conn_];
    memset(events, 0, sizeof(epoll_event) * max_conn_);
    int event_num = epoll_wait(epoll_fd_, events, max_conn_, 0);
    if (event_num < 0) {
        print_error("Epoll failed: epoll_wait()");
        return -1;
    }
    int res_num = 0;
    for (int i = 0; i < event_num; ++i) {
        /* Filter bad tries */
        if (events[i].events & (EPOLLERR | EPOLLHUP)) {
            print_info("Epoll failed: EPOLLERR or EPOLLHUP");
            close(events[i].data.fd);
            continue;
        }
        /* Filter initializations */
        if (events[i].data.fd == listen_fd_) {
            if (!(events[i].events & EPOLLIN)) {
                print_info("Accept failed: no rule for main socket not EPOLLIN");
            } else if (EpollInitialize() < 0) {
                return -1;
            }
            continue;
        }
        /* Handle send & recv */
        EpollCoreContext *context = (EpollCoreContext *)events[i].data.ptr;
        if (events[i].events & EPOLLIN) {
            /* Check completion */
            if (EpollCheck(context) > 0) {
                res_num += 1;
                continue;
            }
            EpollRecv(context);
        } else if (events[i].events & EPOLLOUT) {
            EpollSend(context);
        } else {
            print_info("Epoll failed: unknown event");
        }
    }
    return res_num;
}

int PieSocket::EpollInitialize() {
    int client_fd = accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK);
    if (client_fd < 0) {
        if (errno == EINTR || errno == EWOULDBLOCK) {
            print_error("Accept failed: client give up");
            return 0;
        } else {
            print_error("Accept failed: unknown error");
            return -1;
        }
    }
    EpollCoreContext *context = new EpollCoreContext(client_fd);
    epoll_init_handler_(epoll_udata_, context->GetUserContext());
    context->ResetBuffer();
    EpollState state = context->GetState();
    if (state == kEpollStateError) {
        print_info("Accept failed: InitHandler()");
        EpollComplete(context);
    } else {
        epoll_event new_event;
        memset(&new_event, 0, sizeof(epoll_event));
        new_event.data.fd = client_fd;
        new_event.events = (state == kEpollStateSend) ? EPOLLOUT : EPOLLIN;
        new_event.data.ptr = context;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &new_event) < 0) {
            print_error("Accept failed: epoll_ctl()");
            EpollComplete(context);
        }
    }
    return 0;
}

void PieSocket::EpollSend(EpollCoreContext *context) {
    int ret = context->SendData();
    if (ret < 0) {
        print_error("Epoll-out failed: SendData()");
        EpollComplete(context);
    } else if (ret > 0) {
        epoll_send_handler_(epoll_udata_, context->GetUserContext());
        context->ResetBuffer();
        EpollState state = context->GetState();
        if (state == kEpollStateError) {
            print_info("Epoll-out failed: SendHandler()");
            EpollComplete(context);
        } else if (state != kEpollStateSend) {
            int client_fd = context->GetFd();
            epoll_event new_event;
            memset(&new_event, 0, sizeof(epoll_event));
            new_event.data.fd = client_fd;
            new_event.events = EPOLLIN;
            new_event.data.ptr = context;
            if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd, &new_event) < 0) {
                print_error("Epoll-out failed: epoll_ctl()");
                EpollComplete(context);
            }
        }
    }
}

void PieSocket::EpollRecv(EpollCoreContext *context) {
    int ret = context->RecvData();
    if (ret < 0) {
        print_error("Epoll-in failed: RecvData()");
        EpollComplete(context);
    } else if (ret > 0) {
        epoll_recv_handler_(epoll_udata_, context->GetUserContext());
        context->ResetBuffer();
        EpollState state = context->GetState();
        if (state == kEpollStateError) {
            print_info("Epoll-in failed: RecvHandler()");
            EpollComplete(context);
        } else if (state == kEpollStateSend) {
            int client_fd = context->GetFd();
            epoll_event new_event;
            memset(&new_event, 0, sizeof(epoll_event));
            new_event.data.fd = client_fd;
            new_event.events = EPOLLOUT;
            new_event.data.ptr = context;
            if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd, &new_event) < 0) {
                print_error("Epoll-in failed: epoll_ctl()");
                EpollComplete(context);
            }
        }
    }
}

int PieSocket::EpollCheck(EpollCoreContext *context) {
    int ret = 0;
    if (context->GetState() == kEpollStateComp) {
        ret = context->TryComplete();
        if (ret != 0) {
            EpollComplete(context);
            if (ret < 0) {
                print_error("Epoll-in failed: TryComplete()");
            }
        }
    }
    return ret;
}

void PieSocket::EpollComplete(EpollCoreContext *context) {
    int client_fd = context->GetFd();
    epoll_comp_handler_(epoll_udata_, context->GetUserContext());
    delete context;
    if (client_fd >= 0) {
        close(client_fd);
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
    }
}
