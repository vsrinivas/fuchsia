// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdarg.h>
#include <sys/param.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <threads.h>
#include <unistd.h>

#include <fuchsia/net/c/fidl.h>
#include <zircon/assert.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <lib/zircon-internal/debug.h>
#include <lib/fdio/io.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/zxs/protocol.h>
#include <lib/zxs/zxs.h>

#include "private.h"
#include "private-socket.h"
#include "unistd.h"

static zx_status_t get_service_handle(const char* path, zx_handle_t* saved,
                                      mtx_t* lock, zx_handle_t* out) {
    zx_status_t r;
    zx_handle_t h0, h1;
    mtx_lock(lock);
    if (*saved == ZX_HANDLE_INVALID) {
        if ((r = zx_channel_create(0, &h0, &h1)) != ZX_OK) {
            mtx_unlock(lock);
            return r;
        }
        if ((r = fdio_service_connect(path, h1)) != ZX_OK) {
            mtx_unlock(lock);
            zx_handle_close(h0);
            return r;
        }
        *saved = h0;
    }
    *out = *saved;
    mtx_unlock(lock);
    return ZX_OK;
}

// This wrapper waits for the service to publish the service handle.
// TODO(ZX-1890): move to a better mechanism when available.
static zx_status_t get_service_with_retries(const char* path, zx_handle_t* saved,
                                            mtx_t* lock, zx_handle_t* out) {
    zx_status_t r;
    unsigned retry = 0;
    while ((r = get_service_handle(path, saved, lock, out)) == ZX_ERR_NOT_FOUND) {
        if (retry >= 24) {
            // 10-second timeout
            return ZX_ERR_NOT_FOUND;
        }
        retry++;
        zx_nanosleep(zx_deadline_after((retry < 8) ? ZX_MSEC(250) : ZX_MSEC(500)));
    }
    return r;
}

static zx_status_t get_socket_provider(zx_handle_t* out) {
    static zx_handle_t saved = ZX_HANDLE_INVALID;
    static mtx_t lock = MTX_INIT;
    return get_service_with_retries("/svc/" fuchsia_net_SocketProvider_Name, &saved, &lock, out);
}

__EXPORT
int socket(int domain, int type, int protocol) {
    zx_handle_t socket_provider;
    zx_status_t status = get_socket_provider(&socket_provider);
    if (status != ZX_OK) {
        return ERRNO(EIO);
    }

    int16_t out_code;
    zx_handle_t socket;
    // We're going to manage blocking on the client side, so always ask the
    // provider for a non-blocking socket.
    status = fuchsia_net_SocketProviderSocket(
        socket_provider,
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

    zxs_socket_t out_socket;
    status = zxs_socket(socket, &out_socket);
    if (status != ZX_OK) {
        return ERROR(status);
    }

    fdio_t* io;
    if (out_socket.flags & ZXS_FLAG_DATAGRAM) {
        io = fdio_socket_create_datagram(socket, 0);
    } else {
        io = fdio_socket_create_stream(socket, 0);
    }
    if (io == NULL) {
        return ERRNO(EIO);
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
    const zxs_socket_t* socket = NULL;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    int16_t out_code;
    zx_status_t status = fuchsia_net_SocketControlConnect(
        socket->socket, (const uint8_t*)addr, len, &out_code);
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
            status = zx_object_wait_one(
                socket->socket, ZXSIO_SIGNAL_OUTGOING, ZX_TIME_INFINITE,
                &observed);
            if (status != ZX_OK) {
                fdio_release(io);
                return ERROR(status);
            }
            // Call Connect() again after blocking to find connect's result.
            status = fuchsia_net_SocketControlConnect(
                socket->socket, (const uint8_t*)addr, len, &out_code);
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
    const zxs_socket_t* socket;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    int16_t out_code;
    zx_status_t status = fuchsia_net_SocketControlBind(
        socket->socket, (const uint8_t*)(addr), len, &out_code);
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
    const zxs_socket_t* socket;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    int16_t out_code;
    zx_status_t status = fuchsia_net_SocketControlListen(
        socket->socket, static_cast<int16_t>(backlog), &out_code);
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

    const zxs_socket_t* socket = NULL;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    bool nonblocking = *fdio_get_ioflag(io) & IOFLAG_NONBLOCK;

    int nfd = fdio_reserve_fd(0);
    if (nfd < 0) {
        fdio_release(io);
        return nfd;
    }

    zx_status_t status;
    int16_t out_code;
    zx_handle_t accepted;
    for (;;) {
        // We're going to manage blocking on the client side, so always ask the
        // provider for a non-blocking socket.
        status = fuchsia_net_SocketControlAccept(
            socket->socket,
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
                status = zx_object_wait_one(
                    socket->socket,
                    ZXSIO_SIGNAL_INCOMING | ZX_SOCKET_PEER_CLOSED,
                    ZX_TIME_INFINITE, &observed);
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

        status = zx_socket_accept(socket->socket, &accepted);
        if (status == ZX_ERR_SHOULD_WAIT) {
            // Someone got in before us. If we're a blocking socket, try again.
            if (!nonblocking) {
                zx_signals_t observed;
                status = zx_object_wait_one(
                    socket->socket,
                    ZX_SOCKET_ACCEPT | ZX_SOCKET_PEER_CLOSED,
                    ZX_TIME_INFINITE, &observed);
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
            accepted, &out_code, buf, sizeof(buf), &actual);
        if (status != ZX_OK) {
            zx_handle_close(accepted);
            fdio_release_reserved(nfd);
            return ERROR(status);
        }
        if (out_code) {
            zx_handle_close(accepted);
            fdio_release_reserved(nfd);
            return ERRNO(out_code);
        }
        memcpy(addr, buf, MIN(*len, actual));
        *len = static_cast<socklen_t>(actual);
    }

    fdio_t* accepted_io = fdio_socket_create_stream(accepted, IOFLAG_SOCKET_CONNECTED);
    if (accepted_io == NULL) {
        zx_handle_close(accepted);
        fdio_release_reserved(nfd);
        return ERROR(ZX_ERR_NO_RESOURCES);
    }

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

static int addrinfo_status_to_eai(int32_t status) {
    switch (status) {
    case fuchsia_net_AddrInfoStatus_ok:
        return 0;
    case fuchsia_net_AddrInfoStatus_bad_flags:
        return EAI_BADFLAGS;
    case fuchsia_net_AddrInfoStatus_no_name:
        return EAI_NONAME;
    case fuchsia_net_AddrInfoStatus_again:
        return EAI_AGAIN;
    case fuchsia_net_AddrInfoStatus_fail:
        return EAI_FAIL;
    case fuchsia_net_AddrInfoStatus_no_data:
        return EAI_NONAME;
    case fuchsia_net_AddrInfoStatus_buffer_overflow:
        return EAI_OVERFLOW;
    case fuchsia_net_AddrInfoStatus_system_error:
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

    zx_status_t r;
    zx_handle_t sp;
    r = get_socket_provider(&sp);
    if (r != ZX_OK) {
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


    fuchsia_net_AddrInfoHints ht_storage, *ht;
    memset(&ht_storage, 0, sizeof(ht_storage));
    ht = &ht_storage;
    if (hints == NULL) {
        ht = NULL;
    } else {
        ht->flags = hints->ai_flags;
        ht->family = hints->ai_family;
        ht->sock_type = hints->ai_socktype;
        ht->protocol = hints->ai_protocol;
    }

    fuchsia_net_AddrInfoStatus status = 0;
    uint32_t nres = 0;
    fuchsia_net_AddrInfo ai[4];
    r = fuchsia_net_SocketProviderGetAddrInfo(
          sp, node, node_size, service, service_size, ht, &status, &nres, ai);

    if (r != ZX_OK) {
        errno = fdio_status_to_errno(r);
        return EAI_SYSTEM;
    }
    if (status != fuchsia_net_AddrInfoStatus_ok) {
        int eai = addrinfo_status_to_eai(status);
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
        entry[i].ai.ai_flags    = ai[i].flags;
        entry[i].ai.ai_family   = ai[i].family;
        entry[i].ai.ai_socktype = ai[i].sock_type;
        entry[i].ai.ai_protocol = ai[i].protocol;
        entry[i].ai.ai_addr = (struct sockaddr*) &entry[i].addr_storage;
        entry[i].ai.ai_canonname = NULL; // TODO: support canonname
        switch (entry[i].ai.ai_family) {
            case AF_INET: {
                struct sockaddr_in
                    * addr = (struct sockaddr_in*) entry[i].ai.ai_addr;
                addr->sin_family = AF_INET;
                addr->sin_port = htons(ai[i].port);
                if (ai[i].addr.len > sizeof(ai[i].addr.val)) {
                    free(entry);
                    errno = EIO;
                    return EAI_SYSTEM;
                }
                memcpy(&addr->sin_addr, ai[i].addr.val, ai[i].addr.len);
                entry[i].ai.ai_addrlen = sizeof(struct sockaddr_in);

                break;
            }

            case AF_INET6: {
                struct sockaddr_in6
                    * addr = (struct sockaddr_in6*) entry[i].ai.ai_addr;
                addr->sin6_family = AF_INET6;
                addr->sin6_port = htons(ai[i].port);
                if (ai[i].addr.len > sizeof(ai[i].addr.val)) {
                    free(entry);
                    errno = EIO;
                    return EAI_SYSTEM;
                }
                memcpy(&addr->sin6_addr, ai[i].addr.val, ai[i].addr.len);
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

    const zxs_socket_t* socket = NULL;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    int16_t out_code;
    uint8_t buf[sizeof(struct sockaddr_storage)];
    size_t actual;
    zx_status_t status = fuchsia_net_SocketControlGetSockName(
        socket->socket, &out_code, buf, sizeof(buf), &actual);
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

    const zxs_socket_t* socket = NULL;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    int16_t out_code;
    uint8_t buf[sizeof(struct sockaddr_storage)];
    size_t actual;
    zx_status_t status = fuchsia_net_SocketControlGetPeerName(
        socket->socket, &out_code, buf, sizeof(buf), &actual);
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
    const zxs_socket_t* socket = NULL;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    int16_t out_code;
    size_t actual;
    zx_status_t status = fuchsia_net_SocketControlGetSockOpt(
        socket->socket,
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
    const zxs_socket_t* socket = NULL;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    int16_t out_code;
    zx_status_t status = fuchsia_net_SocketControlSetSockOpt(
        socket->socket,
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
