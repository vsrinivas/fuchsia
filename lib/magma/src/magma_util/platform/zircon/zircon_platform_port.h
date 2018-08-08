// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_port.h"
#include "zircon_platform_semaphore.h"

#include <lib/zx/port.h>
#include <zircon/syscalls/port.h>

namespace magma {

class ZirconPlatformPort : public PlatformPort {
public:
    ZirconPlatformPort(zx::port port) : port_(std::move(port)) {}

    void Close() override
    {
        zx_port_packet_t packet = {};
        packet.type = ZX_PKT_TYPE_USER;
        zx_status_t status = port_.queue(&packet);
        DASSERT(status == ZX_OK);
    }

    Status Wait(uint64_t* key_out, uint64_t timeout_ms) override;

    zx::port& zx_port() { return port_; }

private:
    zx::port port_;
};

} // namespace magma
