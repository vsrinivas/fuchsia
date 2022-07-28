// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_SOCKET_H_
#define LIB_FDIO_SOCKET_H_

#include <fidl/fuchsia.posix.socket.packet/cpp/wire.h>
#include <fidl/fuchsia.posix.socket.raw/cpp/wire.h>
#include <fidl/fuchsia.posix.socket/cpp/wire.h>
#include <lib/zx/status.h>

#include <fbl/ref_ptr.h>

struct fdio;

zx::status<fbl::RefPtr<fdio>> fdio_synchronous_datagram_socket_create(
    zx::eventpair event, fidl::ClientEnd<fuchsia_posix_socket::SynchronousDatagramSocket> client);

zx::status<fbl::RefPtr<fdio>> fdio_datagram_socket_create(
    zx::socket socket, fidl::ClientEnd<fuchsia_posix_socket::DatagramSocket> client,
    size_t tx_meta_buf_size, size_t rx_meta_buf_size);

zx::status<fbl::RefPtr<fdio>> fdio_stream_socket_create(
    zx::socket socket, fidl::ClientEnd<fuchsia_posix_socket::StreamSocket> client);

zx::status<fbl::RefPtr<fdio>> fdio_raw_socket_create(
    zx::eventpair event, fidl::ClientEnd<fuchsia_posix_socket_raw::Socket> client);

zx::status<fbl::RefPtr<fdio>> fdio_packet_socket_create(
    zx::eventpair event, fidl::ClientEnd<fuchsia_posix_socket_packet::Socket> client);

#endif  // LIB_FDIO_SOCKET_H_
