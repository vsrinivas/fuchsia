// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magenta_platform_port.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "magenta/syscalls/port.h"

#include <mx/time.h>

namespace magma {

Status MagentaPlatformPort::Wait(uint64_t* key_out, uint64_t timeout_ms)
{
    if (!port_)
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "wait on invalid port");

    mx_port_packet_t packet;
    mx_status_t status =
        port_.wait(timeout_ms == UINT64_MAX ? MX_TIME_INFINITE : mx::deadline_after(MX_MSEC(timeout_ms)), &packet, 0);
    if (status == MX_ERR_TIMED_OUT)
        return MAGMA_STATUS_TIMED_OUT;

    DLOG("port received key 0x%" PRIx64 " status %d", packet.key, status);

    if (status != MX_OK)
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "port wait returned error: %d", status);

    *key_out = packet.key;
    return MAGMA_STATUS_OK;
}

std::unique_ptr<PlatformPort> PlatformPort::Create()
{
    mx::port port;
    mx_status_t status = mx::port::create(0, &port);
    if (status != MX_OK)
        return DRETP(nullptr, "port::create failed: %d", status);

    return std::make_unique<MagentaPlatformPort>(std::move(port));
}

} // namespace magma
