// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDKTL_PDEV_H_
#define DDKTL_PDEV_H_

#include <ddktl/protocol/clock.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/power.h>

#include <lib/zx/bti.h>
#include <lib/zx/interrupt.h>
#include <optional>
#include <zircon/types.h>

namespace ddk {

class MmioBuffer;

class PDev : public PDevProtocolClient {

public:
    PDev() {}

    // TODO(andresoportus): pass protocol by value/const& so there is no question on lifecycle.
    PDev(pdev_protocol_t* proto) : PDevProtocolClient(proto) {}

    PDev(zx_device_t* parent) : PDevProtocolClient(parent) {}

    ~PDev() = default;

    // Prints out information about the platform device.
    void ShowInfo();

    zx_status_t MapMmio(uint32_t index, std::optional<MmioBuffer>* mmio);

    zx_status_t GetInterrupt(uint32_t index, zx::interrupt* out) {
        return PDevProtocolClient::GetInterrupt(index, 0, out);
    }

    zx_status_t GetBti(uint32_t index, zx::bti* out) {
        return PDevProtocolClient::GetBti(index, out);
    }

    GpioProtocolClient GetGpio(uint32_t index);
    ClockProtocolClient GetClk(uint32_t index);
    PowerProtocolClient GetPower(uint32_t index);
};

} // namespace ddk

#endif // DDKTL_PDEV_H_
