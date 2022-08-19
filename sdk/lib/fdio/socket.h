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

fbl::RefPtr<fdio> fdio_synchronous_datagram_socket_allocate();

fbl::RefPtr<fdio> fdio_datagram_socket_allocate();

zx::status<fbl::RefPtr<fdio>> fdio_stream_socket_create(
    zx::socket socket, fidl::ClientEnd<fuchsia_posix_socket::StreamSocket> client);

fbl::RefPtr<fdio> fdio_raw_socket_allocate();

fbl::RefPtr<fdio> fdio_packet_socket_allocate();

#endif  // LIB_FDIO_SOCKET_H_
