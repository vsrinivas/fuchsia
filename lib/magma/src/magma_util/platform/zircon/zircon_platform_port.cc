// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_port.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "zircon/syscalls/port.h"

#include <lib/zx/time.h>

namespace magma {

Status ZirconPlatformPort::Wait(uint64_t* key_out, uint64_t timeout_ms)
{
    if (!port_)
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "wait on invalid port");

    zx_port_packet_t packet;
    zx_status_t status = port_.wait(
        timeout_ms == UINT64_MAX ? zx::time::infinite() : zx::deadline_after(zx::msec(timeout_ms)),
        &packet);
    if (status == ZX_ERR_TIMED_OUT)
        return DRET_MSG(MAGMA_STATUS_TIMED_OUT, "port wait timed out");

    DLOG("port received key 0x%" PRIx64 " status %d", packet.key, status);

    if (status != ZX_OK)
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "port wait returned error: %d", status);

    if (packet.type == ZX_PKT_TYPE_USER) {
        // Port has been closed.
        port_.reset();
        return MAGMA_STATUS_INTERNAL_ERROR;
    }

    *key_out = packet.key;
    return MAGMA_STATUS_OK;
}

std::unique_ptr<PlatformPort> PlatformPort::Create()
{
    zx::port port;
    zx_status_t status = zx::port::create(0, &port);
    if (status != ZX_OK)
        return DRETP(nullptr, "port::create failed: %d", status);

    return std::make_unique<ZirconPlatformPort>(std::move(port));
}

} // namespace magma
