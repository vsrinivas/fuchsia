// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "connection_destroyer.h"

#include <inttypes.h>

#include "proxy_iostate.h"
#include "src/devices/lib/log/log.h"
#include "zx_device.h"

zx_status_t ConnectionDestroyer::QueueProxyConnection(async_dispatcher_t* dispatcher,
                                                      ProxyIostate* conn) {
  zx_packet_user_t pkt = {};
  pkt.u64[0] = static_cast<uint64_t>(Type::Proxy);
  pkt.u64[1] = reinterpret_cast<uintptr_t>(conn);
  return receiver_.QueuePacket(dispatcher, &pkt);
}

void ConnectionDestroyer::Handler(async_dispatcher_t* dispatcher, async::Receiver* receiver,
                                  zx_status_t status, const zx_packet_user_t* data) {
  Type type = static_cast<Type>(data->u64[0]);
  uintptr_t ptr = data->u64[1];

  switch (type) {
    case Type::Proxy: {
      auto conn = reinterpret_cast<ProxyIostate*>(ptr);
      VLOGF(1, "Destroying proxy connection %p", conn);
      delete conn;
      break;
    }
    default:
      ZX_ASSERT_MSG(false, "Unknown IosDestructionType %" PRIu64 "\n", data->u64[0]);
  }
}
