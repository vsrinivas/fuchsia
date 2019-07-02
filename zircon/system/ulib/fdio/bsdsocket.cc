// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/net/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/zircon-internal/debug.h>
#include <lib/zxs/protocol.h>
#include <lib/zxs/zxs.h>
#include <netdb.h>
#include <poll.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <mutex>

#include "private-socket.h"
#include "private.h"
#include "unistd.h"

namespace fio = ::llcpp::fuchsia::io;
namespace fnet = ::llcpp::fuchsia::net;
namespace fsocket = ::llcpp::fuchsia::posix::socket;

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

__EXPORT
int socket(int domain, int type, int protocol) {
    static fsocket::Provider::SyncClient* provider;

    {
        static std::once_flag once;
        static zx_status_t status;
        std::call_once(once, [&]() {
            zx::channel out;
            status = get_service_handle(fsocket::Provider::Name_, &out);
            if (status != ZX_OK) {
                return;
            }
            static fsocket::Provider::SyncClient client(std::move(out));
            provider = &client;
        });
        if (status != ZX_OK) {
            return ERROR(status);
        }
    }

    int16_t out_code;
    zx::channel created;
    // We're going to manage blocking on the client side, so always ask the
    // provider for a non-blocking socket.
    zx_status_t status =
        provider->Socket(static_cast<int16_t>(domain), static_cast<int16_t>(type) | SOCK_NONBLOCK,
                         static_cast<int16_t>(protocol), &out_code, &created);
    if (status != ZX_OK) {
        return ERROR(status);
    }
    if (out_code) {
        return ERRNO(out_code);
    }
    fsocket::Control::SyncClient control(std::move(created));

    fio::NodeInfo node_info;
    status = control.Describe(&node_info);
    if (status != ZX_OK) {
        return ERROR(status);
    }

    fdio_t* io;
    switch (node_info.which()) {
        case fio::NodeInfo::Tag::kSocket: {
            status = fdio_socket_create(std::move(control),
                                        std::move(node_info.mutable_socket().socket), &io);
            if (status != ZX_OK) {
                return ERROR(status);
            }
            break;
        }
        default:
            return ERROR(ZX_ERR_INTERNAL);
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
    zx_status_t status = socket->control.Connect(
        fidl::VectorView(len, reinterpret_cast<uint8_t*>(const_cast<sockaddr*>(addr))), &out_code);
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
            status =
                socket->socket.wait_one(ZXSIO_SIGNAL_OUTGOING, zx::time::infinite(), &observed);
            if (status != ZX_OK) {
                fdio_release(io);
                return ERROR(status);
            }
            // Call Connect() again after blocking to find connect's result.
            status = socket->control.Connect(
                fidl::VectorView(len, reinterpret_cast<uint8_t*>(const_cast<sockaddr*>(addr))),
                &out_code);
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
    zx_status_t status = socket->control.Bind(
        fidl::VectorView(len, reinterpret_cast<uint8_t*>(const_cast<sockaddr*>(addr))), &out_code);
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
    zx_status_t status = socket->control.Listen(static_cast<int16_t>(backlog), &out_code);
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
int accept4(int fd, struct sockaddr* __restrict addr, socklen_t* __restrict len, int flags) {
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
    zx::channel accepted;
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
            status = socket->control.Accept(static_cast<int16_t>(flags) | SOCK_NONBLOCK, &out_code,
                                            &accepted);
            if (status != ZX_OK) {
                break;
            }

            // This condition should also apply to EAGAIN; it happens to have the
            // same value as EWOULDBLOCK.
            if (out_code == EWOULDBLOCK) {
                if (!nonblocking) {
                    zx_signals_t observed;
                    status = socket->socket.wait_one(ZXSIO_SIGNAL_INCOMING | ZX_SOCKET_PEER_CLOSED,
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
    fsocket::Control::SyncClient control(std::move(accepted));

    if (len) {
        uint8_t response_buffer[fidl::MaxSizeInChannel<fsocket::Control::GetPeerNameResponse>()];
        fidl::DecodeResult result = control.GetPeerName(fidl::BytePart::WrapEmpty(response_buffer));
        if (result.status != ZX_OK) {
            fdio_release_reserved(nfd);
            return ERROR(result.status);
        }
        fsocket::Control::GetPeerNameResponse* response = result.Unwrap();
        if (response->code) {
            fdio_release_reserved(nfd);
            return ERRNO(response->code);
        }
        memcpy(addr, response->addr.data(), MIN(*len, response->addr.count()));
        *len = static_cast<socklen_t>(response->addr.count());
    }

    fio::NodeInfo node_info;
    status = control.Describe(&node_info);
    if (status != ZX_OK) {
        fdio_release_reserved(nfd);
        return ERROR(status);
    }

    fdio_t* accepted_io;
    switch (node_info.which()) {
        case fio::NodeInfo::Tag::kSocket: {
            status = fdio_socket_create(std::move(control),
                                        std::move(node_info.mutable_socket().socket), &accepted_io);
            if (status != ZX_OK) {
                fdio_release_reserved(nfd);
                return ERROR(status);
            }
            break;
        }
        default:
            fdio_release_reserved(nfd);
            return ERROR(ZX_ERR_INTERNAL);
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
int getaddrinfo(const char* __restrict node, const char* __restrict service,
                const struct addrinfo* __restrict hints, struct addrinfo** __restrict res) {
    if ((node == NULL && service == NULL) || res == NULL) {
        errno = EINVAL;
        return EAI_SYSTEM;
    }

    static fnet::SocketProvider::SyncClient* socket_provider;

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
            socket_provider = &client;
        });
        if (status != ZX_OK) {
            errno = EIO;
            return EAI_SYSTEM;
        }
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
    zx_status_t status = socket_provider->GetAddrInfo(fidl::StringView(node_size, node),
                                                      fidl::StringView(service_size, service), ht,
                                                      &info_status, &nres, &ai);

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
        entry[i].ai.ai_canonname = NULL;  // TODO: support canonname
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
void freeaddrinfo(struct addrinfo* res) { free(res); }

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

    uint8_t response_buffer[fidl::MaxSizeInChannel<fsocket::Control::GetSockNameResponse>()];
    fidl::DecodeResult result =
        socket->control.GetSockName(fidl::BytePart::WrapEmpty(response_buffer));
    fdio_release(io);
    if (result.status != ZX_OK) {
        return ERROR(result.status);
    }
    fsocket::Control::GetSockNameResponse* response = result.Unwrap();
    if (response->code) {
        return ERRNO(response->code);
    }
    memcpy(addr, response->addr.data(), MIN(*len, response->addr.count()));
    *len = static_cast<socklen_t>(response->addr.count());
    return response->code;
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

    uint8_t response_buffer[fidl::MaxSizeInChannel<fsocket::Control::GetPeerNameResponse>()];
    fidl::DecodeResult result =
        socket->control.GetPeerName(fidl::BytePart::WrapEmpty(response_buffer));
    fdio_release(io);
    if (result.status != ZX_OK) {
        return ERROR(result.status);
    }
    fsocket::Control::GetPeerNameResponse* response = result.Unwrap();
    if (response->code) {
        return ERRNO(response->code);
    }
    memcpy(addr, response->addr.data(), MIN(*len, response->addr.count()));
    *len = static_cast<socklen_t>(response->addr.count());
    return response->code;
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

    uint8_t request_buffer[fidl::MaxSizeInChannel<fsocket::Control::GetSockOptRequest>()];
    fidl::DecodedMessage<fsocket::Control::GetSockOptRequest> request(
        fidl::BytePart::WrapFull(request_buffer));
    uint8_t response_buffer[fidl::MaxSizeInChannel<fsocket::Control::GetSockOptResponse>()];
    request.message()->level = static_cast<int16_t>(level);
    request.message()->optname = static_cast<int16_t>(optname);
    fidl::DecodeResult result =
        socket->control.GetSockOpt(std::move(request), fidl::BytePart::WrapEmpty(response_buffer));
    fdio_release(io);
    if (result.status != ZX_OK) {
        return ERROR(result.status);
    }
    fsocket::Control::GetSockOptResponse* response = result.Unwrap();
    if (response->code) {
        return ERRNO(response->code);
    }
    if (response->optval.count() > *optlen) {
        return ERRNO(EINVAL);
    }
    memcpy(optval, response->optval.data(), response->optval.count());
    *optlen = static_cast<socklen_t>(response->optval.count());
    return response->code;
}

__EXPORT
int setsockopt(int fd, int level, int optname, const void* optval, socklen_t optlen) {
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
    zx_status_t status = socket->control.SetSockOpt(
        static_cast<int16_t>(level), static_cast<int16_t>(optname),
        fidl::VectorView(optlen, static_cast<uint8_t*>(const_cast<void*>(optval))), &out_code);
    fdio_release(io);
    if (status != ZX_OK) {
        return ERROR(status);
    }
    if (out_code) {
        return ERRNO(out_code);
    }
    return out_code;
}
