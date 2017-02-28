// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_port.h"
#include "magenta_platform_semaphore.h"

#include "magenta/syscalls/port.h"
#include "mx/port.h"

namespace magma {

class MagentaPlatformPort : public PlatformPort {
public:
    MagentaPlatformPort(mx::port port) : port_(std::move(port)) {}

    void Close() override { port_.reset(); }

    Status Wait(uint64_t* key_out, uint64_t timeout_ms) override;

    mx::port& mx_port() { return port_; }

private:
    mx::port port_;
};

} // namespace magma
