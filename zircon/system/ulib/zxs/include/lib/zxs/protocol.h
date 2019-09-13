// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXS_PROTOCOL_H_
#define LIB_ZXS_PROTOCOL_H_

#include <sys/socket.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// wire format for datagram messages
typedef struct fdio_socket_msg {
  struct sockaddr_storage addr;
  socklen_t addrlen;
  int32_t flags;
} fdio_socket_msg_t;

__END_CDECLS

#endif  // LIB_ZXS_PROTOCOL_H_
