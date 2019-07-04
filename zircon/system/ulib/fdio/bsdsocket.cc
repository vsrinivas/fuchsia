// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mutex>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fuchsia/net/c/fidl.h>
#include <fuchsia/net/llcpp/fidl.h>

#include <zircon/assert.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/zircon-internal/debug.h>
#include <lib/zxs/protocol.h>
#include <lib/zxs/zxs.h>

#include "private-socket.h"
#include "private.h"
#include "unistd.h"

namespace fnet = ::llcpp::fuchsia::net;

static zx_status_t get_service_handle(const char path[], zx::channel* out) {
    static zx_handle_t service_root;

    {
        static std::once_flag once;
        static zx_status_t status;
        std::call_once(once, [&]() {
            zx::channel c0, c1;
            status = zx::channel::create(0, &c0, &c1);
            if (status != ZX_OK) {
                return;
            }
            // TODO(abarth): Use "/svc/" once that actually works.
            status = fdio_service_connect("/svc/.", c0.release());
            if (status != ZX_OK) {
                return;
            }
            service_root = c1.release();
        });
        if (status != ZX_OK) {
            return status;
        }
    }

    zx::channel c0, c1;
    zx_status_t status = zx::channel::create(0, &c0, &c1);
    if (status != ZX_OK) {
        return status;
    }

    status = fdio_service_connect_at(service_root, path, c0.release());
    if (status != ZX_OK) {
        return status;
    }
    *out = std::move(c1);
    return ZX_OK;
}

static zx_status_t get_socket_provider(fnet::SocketProvider::SyncClient** out) {
    static fnet::SocketProvider::SyncClient* saved;

    {
        static std::once_flag once;
        static zx_status_t status;
        std::call_once(once, [&]() {
            zx::channel out;
            status = get_service_handle(fnet::SocketProvider::Name_, &out);
            if (status != ZX_OK) {
                return;
            }
            static fnet::SocketProvider::SyncClient client(std::move(out));
            saved = &client;
        });
        if (status != ZX_OK) {
            return status;
        }
    }

    *out = saved;
    return ZX_OK;
}

__EXPORT
int socket(int domain, int type, int protocol) {
    fnet::SocketProvider::SyncClient* socket_provider;
    zx_status_t status = get_socket_provider(&socket_provider);
    if (status != ZX_OK) {
        return ERRNO(EIO);
    }

    int16_t out_code;
    zx::socket socket;
    // We're going to manage blocking on the client side, so always ask the
    // provider for a non-blocking socket.
    status = socket_provider->Socket(
        static_cast<int16_t>(domain),
        static_cast<int16_t>(type) | SOCK_NONBLOCK,
        static_cast<int16_t>(protocol),
        &out_code,
        &socket);
    if (status != ZX_OK) {
        return ERROR(status);
    }
    if (out_code) {
        return ERRNO(out_code);
    }

    zx_info_socket_t info;
    status = socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), NULL, NULL);
    if (status != ZX_OK) {
        return ERROR(status);
    }

    fdio_t* io = fdio_socket_create(std::move(socket), info);
    if (io == NULL) {
        return ERROR(ZX_ERR_NO_RESOURCES);
    }

    if (type & SOCK_NONBLOCK) {
        *fdio_get_ioflag(io) |= IOFLAG_NONBLOCK;
    }

    // TODO(ZX-973): Implement CLOEXEC.
    // if (type & SOCK_CLOEXEC) {
    // }

    int fd = fdio_bind_to_fd(io, -1, 0);
    if (fd < 0) {
        fdio_get_ops(io)->close(io);
        fdio_release(io);
        return ERRNO(EMFILE);
    }
    return fd;
}

__EXPORT
int connect(int fd, const struct sockaddr* addr, socklen_t len) {
    zxs_socket_t* socket;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    int16_t out_code;
    zx_status_t status = fuchsia_net_SocketControlConnect(
        socket->socket.get(), (const uint8_t*)addr, len, &out_code);
    if (status != ZX_OK) {
        fdio_release(io);
        return ERROR(status);
    }
    if (out_code == EINPROGRESS) {
        bool nonblocking = *fdio_get_ioflag(io) & IOFLAG_NONBLOCK;

        if (nonblocking) {
            *fdio_get_ioflag(io) |= IOFLAG_SOCKET_CONNECTING;
        } else {
            zx_signals_t observed;
            status = socket->socket.wait_one(ZXSIO_SIGNAL_OUTGOING, zx::time::infinite(),
                                             &observed);
            if (status != ZX_OK) {
                fdio_release(io);
                return ERROR(status);
            }
            // Call Connect() again after blocking to find connect's result.
            status = fuchsia_net_SocketControlConnect(
                socket->socket.get(), (const uint8_t*)addr, len, &out_code);
            if (status != ZX_OK) {
                fdio_release(io);
                return ERROR(status);
            }
        }
    }

    switch (out_code) {
    case 0: {
        *fdio_get_ioflag(io) |= IOFLAG_SOCKET_CONNECTED;
        fdio_release(io);
        return out_code;
    }

    default: {
        fdio_release(io);
        return ERRNO(out_code);
    }
    }
}

__EXPORT
int bind(int fd, const struct sockaddr* addr, socklen_t len) {
    zxs_socket_t* socket;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    int16_t out_code;
    zx_status_t status = fuchsia_net_SocketControlBind(
        socket->socket.get(), (const uint8_t*)(addr), len, &out_code);
    fdio_release(io);
    if (status != ZX_OK) {
        return ERROR(status);
    }
    if (out_code) {
        return ERRNO(out_code);
    }
    return out_code;
}

__EXPORT
int listen(int fd, int backlog) {
    zxs_socket_t* socket;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    int16_t out_code;
    zx_status_t status = fuchsia_net_SocketControlListen(
        socket->socket.get(), static_cast<int16_t>(backlog), &out_code);
    if (status != ZX_OK) {
        fdio_release(io);
        return ERROR(status);
    }
    if (out_code) {
        fdio_release(io);
        return ERRNO(out_code);
    }

    fdio_release(io);
    return out_code;
}

__EXPORT
int accept4(int fd, struct sockaddr* __restrict addr, socklen_t* __restrict len,
            int flags) {
    if (flags & ~SOCK_NONBLOCK) {
        return ERRNO(EINVAL);
    }
    if ((addr == NULL) != (len == NULL)) {
        return ERRNO(EINVAL);
    }

    int nfd = fdio_reserve_fd(0);
    if (nfd < 0) {
        return nfd;
    }

    zx_status_t status;
    int16_t out_code;
    zx::socket accepted;
    {
        zxs_socket_t* socket;
        fdio_t* io = fd_to_socket(fd, &socket);
        if (io == NULL) {
            fdio_release_reserved(nfd);
            return ERRNO(EBADF);
        }

        bool nonblocking = *fdio_get_ioflag(io) & IOFLAG_NONBLOCK;
        for (;;) {
            // We're going to manage blocking on the client side, so always ask the
            // provider for a non-blocking socket.
            status = fuchsia_net_SocketControlAccept(
                socket->socket.get(),
                static_cast<int16_t>(flags) | SOCK_NONBLOCK,
                &out_code);
            if (status != ZX_OK) {
                break;
            }

            // This condition should also apply to EAGAIN; it happens to have the
            // same value as EWOULDBLOCK.
            if (out_code == EWOULDBLOCK) {
                if (!nonblocking) {
                    zx_signals_t observed;
                    status = socket->socket.wait_one(
                        ZXSIO_SIGNAL_INCOMING | ZX_SOCKET_PEER_CLOSED,
                        zx::time::infinite(), &observed);
                    if (status != ZX_OK) {
                        break;
                    }
                    if (observed & ZXSIO_SIGNAL_INCOMING) {
                        continue;
                    }
                    ZX_ASSERT(observed & ZX_SOCKET_PEER_CLOSED);
                    status = ZX_ERR_PEER_CLOSED;
                    break;
                }
            }
            if (out_code) {
                break;
            }

            status = socket->socket.accept(&accepted);
            if (status == ZX_ERR_SHOULD_WAIT) {
                // Someone got in before us. If we're a blocking socket, try again.
                if (!nonblocking) {
                    zx_signals_t observed;
                    status = socket->socket.wait_one(
                        ZX_SOCKET_ACCEPT | ZX_SOCKET_PEER_CLOSED,
                        zx::time::infinite(), &observed);
                    if (status != ZX_OK) {
                        break;
                    }
                    if (observed & ZX_SOCKET_ACCEPT) {
                        continue;
                    }
                    ZX_ASSERT(observed & ZX_SOCKET_PEER_CLOSED);
                    status = ZX_ERR_PEER_CLOSED;
                }
            }
            break;
        }
        fdio_release(io);
    }

    if (status != ZX_OK) {
        fdio_release_reserved(nfd);
        return ERROR(status);
    }
    if (out_code) {
        fdio_release_reserved(nfd);
        return ERRNO(out_code);
    }

    if (len) {
        int16_t out_code;
        uint8_t buf[sizeof(struct sockaddr_storage)];
        size_t actual;
        zx_status_t status = fuchsia_net_SocketControlGetPeerName(
            accepted.get(), &out_code, buf, sizeof(buf), &actual);
        if (status != ZX_OK) {
            fdio_release_reserved(nfd);
            return ERROR(status);
        }
        if (out_code) {
            fdio_release_reserved(nfd);
            return ERRNO(out_code);
        }
        memcpy(addr, buf, MIN(*len, actual));
        *len = static_cast<socklen_t>(actual);
    }

    zx_info_socket_t info;
    status = accepted.get_info(ZX_INFO_SOCKET, &info, sizeof(info), NULL, NULL);
    if (status != ZX_OK) {
        fdio_release_reserved(nfd);
        return ERROR(status);
    }

    fdio_t* accepted_io = fdio_socket_create(std::move(accepted), info);
    if (accepted_io == NULL) {
        fdio_release_reserved(nfd);
        return ERROR(ZX_ERR_NO_RESOURCES);
    }
    *fdio_get_ioflag(accepted_io) |= IOFLAG_SOCKET_CONNECTED;

    if (flags & SOCK_NONBLOCK) {
        *fdio_get_ioflag(accepted_io) |= IOFLAG_NONBLOCK;
    }

    nfd = fdio_assign_reserved(nfd, accepted_io);
    if (nfd < 0) {
        fdio_get_ops(accepted_io)->close(accepted_io);
        fdio_release(accepted_io);
    }
    return nfd;
}

static int addrinfo_status_to_eai(fnet::AddrInfoStatus status) {
    switch (status) {
    case fnet::AddrInfoStatus::ok:
        return 0;
    case fnet::AddrInfoStatus::bad_flags:
        return EAI_BADFLAGS;
    case fnet::AddrInfoStatus::no_name:
        return EAI_NONAME;
    case fnet::AddrInfoStatus::again:
        return EAI_AGAIN;
    case fnet::AddrInfoStatus::fail:
        return EAI_FAIL;
    case fnet::AddrInfoStatus::no_data:
        return EAI_NONAME;
    case fnet::AddrInfoStatus::buffer_overflow:
        return EAI_OVERFLOW;
    case fnet::AddrInfoStatus::system_error:
        return EAI_SYSTEM;
    default:
        // unknown status
        return EAI_SYSTEM;
    }
}

__EXPORT
int getaddrinfo(const char* __restrict node,
                const char* __restrict service,
                const struct addrinfo* __restrict hints,
                struct addrinfo** __restrict res) {
    if ((node == NULL && service == NULL) || res == NULL) {
        errno = EINVAL;
        return EAI_SYSTEM;
    }

    fnet::SocketProvider::SyncClient* socket_provider;
    zx_status_t status = get_socket_provider(&socket_provider);
    if (status != ZX_OK) {
        errno = EIO;
        return EAI_SYSTEM;
    }

    size_t node_size = 0;
    if (node) {
        node_size = strlen(node);
    }
    size_t service_size = 0;
    if (service) {
        service_size = strlen(service);
    }

    fnet::AddrInfoHints ht_storage, *ht;
    if (hints != nullptr) {
        ht_storage = {
            .flags = hints->ai_flags,
            .family = hints->ai_family,
            .sock_type = hints->ai_socktype,
            .protocol = hints->ai_protocol,
        };
        ht = &ht_storage;
    } else {
        ht = nullptr;
    }

    fnet::AddrInfoStatus info_status;
    uint32_t nres = 0;
    fidl::Array<fnet::AddrInfo, 4> ai;
    status = socket_provider->GetAddrInfo(
        fidl::StringView(node_size, node),
        fidl::StringView(service_size, service),
        ht,
        &info_status,
        &nres,
        &ai);

    if (status != ZX_OK) {
        ERROR(status);
        return EAI_SYSTEM;
    }
    int eai = addrinfo_status_to_eai(info_status);
    if (eai) {
        if (eai == EAI_SYSTEM) {
            errno = EIO;
            return EAI_SYSTEM;
        }
        return eai;
    }
    if (nres > 4) {
        errno = EIO;
        return EAI_SYSTEM;
    }

    struct res_entry {
        struct addrinfo ai;
        struct sockaddr_storage addr_storage;
    };
    struct res_entry* entry = static_cast<res_entry*>(calloc(nres, sizeof(struct res_entry)));

    for (uint8_t i = 0; i < nres; i++) {
        entry[i].ai.ai_flags = ai[i].flags;
        entry[i].ai.ai_family = ai[i].family;
        entry[i].ai.ai_socktype = ai[i].sock_type;
        entry[i].ai.ai_protocol = ai[i].protocol;
        entry[i].ai.ai_addr = (struct sockaddr*)&entry[i].addr_storage;
        entry[i].ai.ai_canonname = NULL; // TODO: support canonname
        switch (entry[i].ai.ai_family) {
        case AF_INET: {
            struct sockaddr_in* addr = (struct sockaddr_in*)entry[i].ai.ai_addr;
            addr->sin_family = AF_INET;
            addr->sin_port = htons(ai[i].port);
            if (ai[i].addr.len > sizeof(ai[i].addr.val)) {
                free(entry);
                errno = EIO;
                return EAI_SYSTEM;
            }
            memcpy(&addr->sin_addr, ai[i].addr.val.data(), ai[i].addr.len);
            entry[i].ai.ai_addrlen = sizeof(struct sockaddr_in);

            break;
        }

        case AF_INET6: {
            struct sockaddr_in6* addr = (struct sockaddr_in6*)entry[i].ai.ai_addr;
            addr->sin6_family = AF_INET6;
            addr->sin6_port = htons(ai[i].port);
            if (ai[i].addr.len > sizeof(ai[i].addr.val)) {
                free(entry);
                errno = EIO;
                return EAI_SYSTEM;
            }
            memcpy(&addr->sin6_addr, ai[i].addr.val.data(), ai[i].addr.len);
            entry[i].ai.ai_addrlen = sizeof(struct sockaddr_in6);

            break;
        }

        default: {
            free(entry);
            errno = EIO;
            return EAI_SYSTEM;
        }
        }
    }
    struct addrinfo* next = NULL;
    for (int i = nres - 1; i >= 0; --i) {
        entry[i].ai.ai_next = next;
        next = &entry[i].ai;
    }
    *res = next;

    return 0;
}

__EXPORT
void freeaddrinfo(struct addrinfo* res) {
    free(res);
}

__EXPORT
int getsockname(int fd, struct sockaddr* __restrict addr, socklen_t* __restrict len) {
    if (len == NULL || addr == NULL) {
        return ERRNO(EINVAL);
    }

    zxs_socket_t* socket;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    int16_t out_code;
    uint8_t buf[sizeof(struct sockaddr_storage)];
    size_t actual;
    zx_status_t status = fuchsia_net_SocketControlGetSockName(
        socket->socket.get(), &out_code, buf, sizeof(buf), &actual);
    fdio_release(io);
    if (status != ZX_OK) {
        return ERROR(status);
    }
    if (out_code) {
        return ERRNO(out_code);
    }
    memcpy(addr, buf, MIN(*len, actual));
    *len = static_cast<socklen_t>(actual);
    return out_code;
}

__EXPORT
int getpeername(int fd, struct sockaddr* __restrict addr, socklen_t* __restrict len) {
    if (len == NULL || addr == NULL) {
        return ERRNO(EINVAL);
    }

    zxs_socket_t* socket;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    int16_t out_code;
    uint8_t buf[sizeof(struct sockaddr_storage)];
    size_t actual;
    zx_status_t status = fuchsia_net_SocketControlGetPeerName(
        socket->socket.get(), &out_code, buf, sizeof(buf), &actual);
    fdio_release(io);
    if (status != ZX_OK) {
        return ERROR(status);
    }
    if (out_code) {
        return ERRNO(out_code);
    }
    memcpy(addr, buf, MIN(*len, actual));
    *len = static_cast<socklen_t>(actual);
    return out_code;
}

__EXPORT
int getsockopt(int fd, int level, int optname, void* __restrict optval,
               socklen_t* __restrict optlen) {
    zxs_socket_t* socket;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    // Handle client-maintained socket options.
    if (level == SOL_SOCKET && optname == SO_RCVTIMEO) {
        if (optlen == NULL || *optlen < sizeof(struct timeval)) {
            return ERRNO(EINVAL);
        }
        *optlen = sizeof(struct timeval);
        struct timeval* duration_tv = static_cast<struct timeval*>(optval);
        if (socket->rcvtimeo == zx::duration::infinite()) {
            duration_tv->tv_sec = 0;
            duration_tv->tv_usec = 0;
        } else {
            duration_tv->tv_sec = socket->rcvtimeo.to_secs();
            duration_tv->tv_usec = (socket->rcvtimeo - zx::sec(duration_tv->tv_sec)).to_usecs();
        }
        return 0;
    }

    int16_t out_code;
    size_t actual;
    zx_status_t status = fuchsia_net_SocketControlGetSockOpt(
        socket->socket.get(),
        static_cast<int16_t>(level),
        static_cast<int16_t>(optname),
        &out_code,
        static_cast<uint8_t*>(optval),
        *optlen,
        &actual);
    fdio_release(io);
    if (status != ZX_OK) {
        return ERROR(status);
    }
    if (out_code) {
        return ERRNO(out_code);
    }
    *optlen = static_cast<socklen_t>(actual);
    return out_code;
}

__EXPORT
int setsockopt(int fd, int level, int optname, const void* optval,
               socklen_t optlen) {
    zxs_socket_t* socket;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    // Handle client-maintained socket options.
    if (level == SOL_SOCKET && optname == SO_RCVTIMEO) {
        if (optlen < sizeof(struct timeval)) {
            return ERRNO(EINVAL);
        }
        const struct timeval* duration_tv = static_cast<const struct timeval*>(optval);
        if (duration_tv->tv_sec || duration_tv->tv_usec) {
            socket->rcvtimeo = zx::sec(duration_tv->tv_sec) + zx::usec(duration_tv->tv_usec);
        } else {
            socket->rcvtimeo = zx::duration::infinite();
        }
        return 0;
    }

    int16_t out_code;
    zx_status_t status = fuchsia_net_SocketControlSetSockOpt(
        socket->socket.get(),
        static_cast<int16_t>(level),
        static_cast<int16_t>(optname),
        static_cast<uint8_t*>(const_cast<void*>(optval)),
        optlen,
        &out_code);
    fdio_release(io);
    if (status != ZX_OK) {
        return ERROR(status);
    }
    if (out_code) {
        return ERRNO(out_code);
    }
    return out_code;
}
