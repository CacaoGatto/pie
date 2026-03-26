#ifndef PLUGGED_IN_EPOLL_SOCKET_H
#define PLUGGED_IN_EPOLL_SOCKET_H

#include <cstdint>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>

class PieSocket {
private:
    enum EpollState {
        kEpollStateError = -1,
        kEpollStateComp = 0,
        kEpollStateRecv,
        kEpollStateSend
    };

public:
    class PieBuffer {
        friend class PieSocket;
    public:
        PieBuffer &SetSend() { state_ = kEpollStateSend; return *this; }
        PieBuffer &SetRecv() { state_ = kEpollStateRecv; return *this; }
        PieBuffer &SetComp() { state_ = kEpollStateComp; return *this; }
        PieBuffer &SetSize(uint64_t size) { size_ = size; return *this; }
        PieBuffer &SetAddr(void *addr) { addr_ = addr; return *this; }
        uint64_t GetSize() { return size_; }
        void *GetAddr() { return addr_; }
    private:
        PieBuffer() {}
        ~PieBuffer() {}
        uint64_t size_ = 0;
        void *addr_ = nullptr;
        EpollState state_ = kEpollStateComp;
    };

    struct EpollUserContext {
        PieBuffer buffer;
        void *context = nullptr;
    };

    PieSocket();
    ~PieSocket();

    int ConnectServer(const char *ip_addr, const uint16_t port = kPieDefaultPort);
    int DisconnectServer(int sock_fd);

    int Listen(int max_conn, const char *ip_addr, const uint16_t port = kPieDefaultPort);
    int StopListen();

    int AcceptClient();
    int FinishClient(int sock_fd);

    int SendBuf(int sock_fd, void *buffer, uint64_t size);
    int RecvBuf(int sock_fd, void *buffer, uint64_t *size);

    PieSocket &RegisterInit(void (*handler)(void *udata, EpollUserContext *ctx)) {
        epoll_init_handler_ = handler;
        return *this;
    }
    PieSocket &RegisterSend(void (*handler)(void *udata, EpollUserContext *ctx)) {
        epoll_send_handler_ = handler;
        return *this;
    }
    PieSocket &RegisterRecv(void (*handler)(void *udata, EpollUserContext *ctx)) {
        epoll_recv_handler_ = handler;
        return *this;
    }
    PieSocket &RegisterComp(void (*handler)(void *udata, EpollUserContext *ctx)) {
        epoll_comp_handler_ = handler;
        return *this;
    }
    PieSocket &RegisterUdata(void *udata) {
        epoll_udata_ = udata;
        return *this;
    }

    int EpollLaunch();
    int EpollRetrive();

private:
    class EpollCoreContext {
    public:
        EpollCoreContext(int32_t sock_fd) : sock_fd_(sock_fd) {
            ucontext_ = new EpollUserContext;
            ResetBuffer();
        }
        ~EpollCoreContext() {
            delete ucontext_;
        }
        inline EpollUserContext *GetUserContext() { return ucontext_; }
        inline int32_t GetFd() const { return sock_fd_; }
        inline EpollState GetState() { return ucontext_->buffer.state_; }
        inline void ResetBuffer() {
            ctrl_ptr_ = &ucontext_->buffer.size_;
            ctrl_size_ = sizeof(uint64_t);
            msg_size_done_ = false;
        }
        inline int RecvData() {
            int ret = recv_once();
            if (!msg_size_done_) {
                if (ret <= 0) {
                    return ret;
                }
                msg_size_done_ = true;
                ctrl_size_ = ucontext_->buffer.size_;
                ctrl_ptr_ = ucontext_->buffer.addr_;
                ret = recv_once();
            }
            return ret;
        }
        inline int SendData() {
            int ret = send_once();
            if (!msg_size_done_) {
                if (ret <= 0) {
                    return ret;
                }
                msg_size_done_ = true;
                ctrl_size_ = ucontext_->buffer.size_;
                ctrl_ptr_ = ucontext_->buffer.addr_;
                ret = send_once();
            }
            return ret;
        }
        inline int TryComplete() {
            char fin[64];
            int ret = read(sock_fd_, fin, 64);
            if (ret < 0) {
                if (errno == EINTR || errno == EWOULDBLOCK) {
                    return 0;  // Asynchronous socket
                } else {
                    return -1;
                }
            } else if (ret > 0) {
                return 0;  // Ignore garbage data after shutdown
            }
            return 1;
        }
    private:
        const int32_t sock_fd_ = -1;
        EpollUserContext *ucontext_;
        uint64_t ctrl_size_;
        void *ctrl_ptr_;
        bool msg_size_done_;

        inline int recv_once() {
            int ret = read(sock_fd_, ctrl_ptr_, ctrl_size_);
            if (ret < 0) {
                if (errno == EINTR || errno == EWOULDBLOCK) {
                    return 0;
                } else {
                    return -1;
                }
            } else if (ret == 0) {
                return -1;
            }
            ctrl_size_ -= ret;
            ctrl_ptr_ = (char *)ctrl_ptr_ + ret;
            if (ctrl_size_) {
                return 0;
            }
            return 1;
        }
        inline int send_once() {
            int ret = write(sock_fd_, ctrl_ptr_, ctrl_size_);
            if (ret < 0) {
                if (errno == EINTR || errno == EWOULDBLOCK) {
                    return 0;
                } else {
                    return -1;
                }
            }
            ctrl_size_ -= ret;
            ctrl_ptr_ = (char *)ctrl_ptr_ + ret;
            if (ctrl_size_) {
                return 0;
            }
            return 1;
        }
    };

    static constexpr uint16_t kPieDefaultPort = 7654;

    int listen_fd_ = -1;
    int epoll_fd_ = -1;
    int max_conn_ = -1;

    void (*epoll_init_handler_)(void *udata, EpollUserContext *ctx) = nullptr;
    void (*epoll_recv_handler_)(void *udata, EpollUserContext *ctx) = nullptr;
    void (*epoll_send_handler_)(void *udata, EpollUserContext *ctx) = nullptr;
    void (*epoll_comp_handler_)(void *udata, EpollUserContext *ctx) = nullptr;
    void *epoll_udata_ = nullptr;

    int EpollInitialize();
    void EpollSend(EpollCoreContext *context);
    void EpollRecv(EpollCoreContext *context);
    int EpollCheck(EpollCoreContext *context);
    void EpollComplete(EpollCoreContext *context);
};

#endif // PLUGGED_IN_EPOLL_SOCKET_H