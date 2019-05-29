// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>
#include <lib/driver-integration-test/fixture.h>
#include <fbl/string.h>

namespace usb_virtual_bus {

using driver_integration_test::IsolatedDevmgr;

class USBVirtualBus {
public:
    USBVirtualBus();

    static zx_status_t create(USBVirtualBus* bus) { return ZX_OK; }

    // Initialize UMS. Asserts on failure.
    void InitUMS(fbl::String* devpath);

    // Initialize a Usb HID device. Asserts on failure.
    void InitUsbHid(fbl::String* devpath);

    void GetHandles(zx::unowned_channel* peripheral, zx::unowned_channel* bus);

    int GetRootFd();

private:
    IsolatedDevmgr::Args args_;
    IsolatedDevmgr devmgr_;
    zx::channel peripheral_;
    zx::channel virtual_bus_handle_;
    DISALLOW_COPY_ASSIGN_AND_MOVE(USBVirtualBus);
};

} // namespace usb_virtual_bus
