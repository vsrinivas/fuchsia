// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_semaphore.h"
#include "platform_port.h"

#include "zircon/syscalls/port.h"
#include "zx/port.h"

namespace magma {

class ZirconPlatformPort : public PlatformPort {
public:
    ZirconPlatformPort(zx::port port) : port_(std::move(port)) {}

    void Close() override { port_.reset(); }

    Status Wait(uint64_t* key_out, uint64_t timeout_ms) override;

    zx::port& zx_port() { return port_; }

private:
    zx::port port_;
};

} // namespace magma
