#ifndef PLUGGED_IN_EPOLL_SOCKET_H
#define PLUGGED_IN_EPOLL_SOCKET_H

#include <cstdint>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <atomic>

namespace pie {

class EpollBuffer {
    friend class PieSocket;
public:
    EpollBuffer &SetSend() { state_ = kEpollStateSend; return *this; }
    EpollBuffer &SetRecv() { state_ = kEpollStateRecv; return *this; }
    EpollBuffer &SetComp() { state_ = kEpollStateComp; return *this; }
    EpollBuffer &SetSize(uint64_t size) { size_ = size; return *this; }
    EpollBuffer &SetAddr(void *addr) { addr_ = addr; return *this; }
    uint64_t GetSize() { return size_; }
    void *GetAddr() { return addr_; }
private:
    EpollBuffer() {}
    ~EpollBuffer() {}
    uint64_t size_ = 0;
    void *addr_ = nullptr;
    enum EpollState {
        kEpollStateError = -1,
        kEpollStateComp = 0,
        kEpollStateRecv,
        kEpollStateSend
    } state_ = kEpollStateComp;
};

typedef void *(*EpollHandler)(void *sock_ctx, void *task_ctx, EpollBuffer &buffer);

class PieSocket {
public:
    PieSocket();
    ~PieSocket();

    int ConnectServer(const char *ip_addr, uint16_t port = kPieDefaultPort);
    int DisconnectServer(int sock_fd);

    int Listen(int max_conn, const char *ip_addr, uint16_t port = kPieDefaultPort);
    int StopListen();
    inline int GetClientCount() { return living_conn_.load(); }

    int AcceptClient();
    int FinishClient(int sock_fd);

    int SendBuf(int sock_fd, void *buffer, uint64_t size);
    int RecvBuf(int sock_fd, void *buffer, uint64_t *size);

    PieSocket &RegisterInit(EpollHandler handler) {
        epoll_init_handler_ = handler;
        return *this;
    }
    PieSocket &RegisterSend(EpollHandler handler) {
        epoll_send_handler_ = handler;
        return *this;
    }
    PieSocket &RegisterRecv(EpollHandler handler) {
        epoll_recv_handler_ = handler;
        return *this;
    }
    PieSocket &RegisterComp(EpollHandler handler) {
        epoll_comp_handler_ = handler;
        return *this;
    }
    PieSocket &RegisterContext(void *ctx) {
        sock_ctx_ = ctx;
        return *this;
    }

    int EpollLaunch();
    int EpollRetrive();

private:
    class EpollCoreContext {
    public:
        EpollCoreContext(int32_t sock_fd) : sock_fd_(sock_fd) {
            ResetBuffer();
        }
        ~EpollCoreContext() {
        }
        static constexpr int kCompletion = -1234;
        inline void SetTaskContext(void *ctx) { task_ctx_ = ctx; }
        inline void *GetTaskContext() const { return task_ctx_; }
        inline EpollBuffer &GetBuffer() { return buffer_; }
        inline int32_t GetFd() const { return sock_fd_; }
        inline bool IsError() const {
            return buffer_.state_ == EpollBuffer::kEpollStateError;
        }
        inline bool IsSend() const {
            return buffer_.state_ == EpollBuffer::kEpollStateSend;
        }
        inline bool IsComp() const {
            return buffer_.state_ == EpollBuffer::kEpollStateComp;
        }
        inline void ResetBuffer() {
            ctrl_ptr_ = &buffer_.size_;
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
                ctrl_size_ = buffer_.size_;
                ctrl_ptr_ = buffer_.addr_;
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
                ctrl_size_ = buffer_.size_;
                ctrl_ptr_ = buffer_.addr_;
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
        EpollBuffer buffer_;
        void *task_ctx_ = nullptr;
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
                return kCompletion;
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
    std::atomic<int> living_conn_{0};

    EpollHandler epoll_init_handler_ = nullptr;
    EpollHandler epoll_recv_handler_ = nullptr;
    EpollHandler epoll_send_handler_ = nullptr;
    EpollHandler epoll_comp_handler_ = nullptr;
    void *sock_ctx_ = nullptr;

    int EnableNonblocking(int sock_fd);
    int CreateSocket(const char *ip_addr, uint16_t port,
                     sockaddr_in *sock_addr);
    int RecvData(int sock_fd, void *data, size_t size);
    int SendData(int sock_fd, void *data, size_t size);

    int EpollInitialize();
    int EpollSend(EpollCoreContext *context);
    int EpollRecv(EpollCoreContext *context);
    int EpollCheck(EpollCoreContext *context);
    void EpollComplete(EpollCoreContext *context);
};

}

#endif // PLUGGED_IN_EPOLL_SOCKET_H