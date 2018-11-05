// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <threads.h>
#include <unistd.h>

#include <fuchsia/net/c/fidl.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <lib/fdio/debug.h>
#include <lib/fdio/io.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/util.h>
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
    return get_service_with_retries("/svc/fuchsia.net.LegacySocketProvider", &saved, &lock, out);
}

__EXPORT
int socket(int domain, int type, int protocol) {
    fdio_t* io = NULL;
    zx_status_t r;

    zx_handle_t sp;
    r = get_socket_provider(&sp);
    if (r != ZX_OK) {
        return ERRNO(EIO);
    }

    zx_handle_t s = ZX_HANDLE_INVALID;
    int32_t rr = 0;
    r = fuchsia_net_LegacySocketProviderOpenSocket(
        sp, domain, type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC), protocol, &s, &rr);

    if (r != ZX_OK) {
        return ERRNO(EIO);
    }
    if (rr != ZX_OK) {
        return STATUS(rr);
    }

    if (type & SOCK_DGRAM) {
        io = fdio_socket_create_datagram(s, 0);
    } else {
        io = fdio_socket_create_stream(s, 0);
    }

    if (io == NULL) {
        return ERRNO(EIO);
    }

    if (type & SOCK_NONBLOCK) {
        io->ioflag |= IOFLAG_NONBLOCK;
    }

    // TODO(ZX-973): Implement CLOEXEC.
    // if (type & SOCK_CLOEXEC) {
    // }

    int fd;
    if ((fd = fdio_bind_to_fd(io, -1, 0)) < 0) {
        io->ops->close(io);
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

    zx_status_t status = zxs_connect(socket, addr, len);
    if (status == ZX_ERR_SHOULD_WAIT) {
        io->ioflag |= IOFLAG_SOCKET_CONNECTING;
        fdio_release(io);
        return ERRNO(EINPROGRESS);
    } else if (status == ZX_OK) {
        io->ioflag |= IOFLAG_SOCKET_CONNECTED;
    }
    fdio_release(io);
    return STATUS(status);
}

__EXPORT
int bind(int fd, const struct sockaddr* addr, socklen_t len) {
    const zxs_socket_t* socket;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    zx_status_t status = zxs_bind(socket, addr, len);
    fdio_release(io);
    return STATUS(status);
}

__EXPORT
int listen(int fd, int backlog) {
    const zxs_socket_t* socket;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    zx_status_t status = zxs_listen(socket, backlog);

    if (status == ZX_OK) {
        zxsio_t* sio = (zxsio_t*)io;
        sio->flags |= ZXSIO_DID_LISTEN;
    }

    fdio_release(io);
    return STATUS(status);
}

__EXPORT
int accept4(int fd, struct sockaddr* restrict addr, socklen_t* restrict len,
            int flags) {
    if (flags & ~SOCK_NONBLOCK) {
        return ERRNO(EINVAL);
    }

    const zxs_socket_t* socket = NULL;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    zxsio_t* sio = (zxsio_t*)io;
    if (!(sio->flags & ZXSIO_DID_LISTEN)) {
        fdio_release(io);
        return ERROR(ZX_ERR_BAD_STATE);
    }

    size_t actual = 0u;
    zxs_socket_t accepted;
    memset(&accepted, 0, sizeof(accepted));
    zx_status_t status = zxs_accept(socket, addr, len ? *len : 0u, &actual, &accepted);
    fdio_release(io);
    if (status == ZX_ERR_SHOULD_WAIT) {
        return ERRNO(EWOULDBLOCK);
    } else if (status != ZX_OK) {
        return ERROR(status);
    }

    fdio_t* io2 = NULL;
    if ((io2 = fdio_socket_create_stream(accepted.socket, IOFLAG_SOCKET_CONNECTED)) == NULL) {
        return ERROR(ZX_ERR_NO_RESOURCES);
    }

    if (flags & SOCK_NONBLOCK) {
        io2->ioflag |= IOFLAG_NONBLOCK;
    }

    if (len != NULL) {
        *len = actual;
    }

    int fd2;
    if ((fd2 = fdio_bind_to_fd(io2, -1, 0)) < 0) {
        io->ops->close(io2);
        fdio_release(io2);
        return ERRNO(EMFILE);
    }
    return fd2;
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

    fuchsia_net_String sn_storage, *sn;
    memset(&sn_storage, 0, sizeof(sn_storage));
    sn = &sn_storage;
    if (node == NULL) {
        sn = NULL;
    } else {
        size_t len = strlen(node);
        if (len > sizeof(sn->val)) {
            errno = EINVAL;
            return EAI_SYSTEM;
        }
        memcpy(sn->val, node, len);
        sn->len = len;
    }

    fuchsia_net_String ss_storage, *ss;
    memset(&ss_storage, 0, sizeof(ss_storage));
    ss = &ss_storage;
    if (service == NULL) {
        ss = NULL;
    } else {
        size_t len = strlen(service);
        if (len > sizeof(ss->val)) {
            errno = EINVAL;
            return EAI_SYSTEM;
        }
        memcpy(ss->val, service, len);
        ss->len = len;
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
    int32_t nres = 0;
    fuchsia_net_AddrInfo ai[4];
    r = fuchsia_net_LegacySocketProviderGetAddrInfo(
          sp, sn, ss, ht, &status, &nres, &ai[0], &ai[1], &ai[2], &ai[3]);

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
    if (nres < 0 || nres > 4) {
        errno = EIO;
        return EAI_SYSTEM;
    }

    struct res_entry {
        struct addrinfo ai;
        struct sockaddr_storage addr_storage;
    };
    struct res_entry* entry = calloc(nres, sizeof(struct res_entry));

    for (int i = 0; i < nres; i++) {
        entry[i].ai.ai_flags = ai[i].flags;
        entry[i].ai.ai_family = ai[i].family;
        entry[i].ai.ai_socktype = ai[i].sock_type;
        entry[i].ai.ai_protocol = ai[i].protocol;
        entry[i].ai.ai_addr = (struct sockaddr*)&entry[i].addr_storage;
        entry[i].ai.ai_canonname = NULL; // TODO: support canonname
        if (entry[i].ai.ai_family == AF_INET) {
            struct sockaddr_in* addr = (struct sockaddr_in*)entry[i].ai.ai_addr;
            addr->sin_family = AF_INET;
            addr->sin_port = htons(ai[i].port);
            if (ai[i].addr.len > sizeof(ai[i].addr.val)) {
                free(entry);
                errno = EIO;
                return EAI_SYSTEM;
            }
            memcpy(&addr->sin_addr, ai[i].addr.val, ai[i].addr.len);
            entry[i].ai.ai_addrlen = sizeof(struct sockaddr_in);
        } else if (entry[i].ai.ai_family == AF_INET6) {
            struct sockaddr_in6* addr = (struct sockaddr_in6*)entry[i].ai.ai_addr;
            addr->sin6_family = AF_INET6;
            addr->sin6_port = htons(ai[i].port);
            if (ai[i].addr.len > sizeof(ai[i].addr.val)) {
                free(entry);
                errno = EIO;
                return EAI_SYSTEM;
            }
            memcpy(&addr->sin6_addr, ai[i].addr.val, ai[i].addr.len);
            entry[i].ai.ai_addrlen = sizeof(struct sockaddr_in6);
        } else {
            free(entry);
            errno = EIO;
            return EAI_SYSTEM;
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
int getsockname(int fd, struct sockaddr* restrict addr, socklen_t* restrict len) {
    if (len == NULL || addr == NULL) {
        return ERRNO(EINVAL);
    }

    const zxs_socket_t* socket = NULL;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    size_t actual = 0u;
    zx_status_t status = zxs_getsockname(socket, addr, *len, &actual);
    if (status == ZX_OK) {
        *len = actual;
    }
    fdio_release(io);
    return STATUS(status);
}

__EXPORT
int getpeername(int fd, struct sockaddr* restrict addr, socklen_t* restrict len) {
    if (len == NULL || addr == NULL) {
        return ERRNO(EINVAL);
    }

    const zxs_socket_t* socket = NULL;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    size_t actual = 0u;
    zx_status_t status = zxs_getpeername(socket, addr, *len, &actual);
    if (status == ZX_OK) {
        *len = actual;
    }
    fdio_release(io);
    return STATUS(status);
}

__EXPORT
int getsockopt(int fd, int level, int optname, void* restrict optval,
               socklen_t* restrict optlen) {
    const zxs_socket_t* socket = NULL;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    zx_status_t r;
    if (level == SOL_SOCKET && optname == SO_ERROR) {
        if (optval == NULL || optlen == NULL || *optlen < sizeof(int)) {
            r = ZX_ERR_INVALID_ARGS;
        } else {
            zx_status_t status;
            size_t actual = 0u;
            r = zxs_getsockopt(socket, SOL_SOCKET, SO_ERROR, &status,
                               sizeof(status), &actual);
            if (r == ZX_OK) {
                int errno_ = 0;
                if (status != ZX_OK) {
                    errno_ = fdio_status_to_errno(status);
                }
                *(int*)optval = errno_;
                *optlen = sizeof(int);
            }
        }
    } else {
        if (optval == NULL || optlen == NULL) {
            r = ZX_ERR_INVALID_ARGS;
        } else {
            size_t actual = 0u;
            r = zxs_getsockopt(socket, level, optname, optval, *optlen, &actual);
            if (r == ZX_OK) {
                *optlen = actual;
            }
        }
    }
    fdio_release(io);

    return STATUS(r);
}

__EXPORT
int setsockopt(int fd, int level, int optname, const void* optval,
               socklen_t optlen) {
    const zxs_socket_t* socket = NULL;
    fdio_t* io = fd_to_socket(fd, &socket);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    zxs_option_t option = {
        .level = level,
        .name = optname,
        .value = optval,
        .length = optlen,
    };

    zx_status_t status = zxs_setsockopts(socket, &option, 1u);
    fdio_release(io);
    return STATUS(status);
}
