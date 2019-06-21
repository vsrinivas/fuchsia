// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver-unit-test/utils.h>

#include <fbl/auto_call.h>
#include <lib/driver-unit-test/logger.h>
#include <zxtest/zxtest.h>

namespace {
    zx_device_t* parent_device = nullptr;
}  // namespace

namespace driver_unit_test {

void SetParent(zx_device_t* parent) {
    parent_device = parent;
}

zx_device_t* GetParent() {
    return parent_device;
}

bool RunZxTests(const char* name, zx_device_t* parent, zx_handle_t channel) {
    SetParent(parent);

    auto cleanup = fbl::MakeAutoCall([]() {
        SetParent(nullptr);
        Logger::DeleteInstance();
    });

    if (channel != ZX_HANDLE_INVALID) {
        zx_status_t status = Logger::CreateInstance(zx::unowned_channel(channel));
        if (status == ZX_OK) {
            zxtest::Runner::GetInstance()->AddObserver(driver_unit_test::Logger::GetInstance());
        }
    }
    const int kArgc = 1;
    const char* argv[kArgc] = {name};
    return RUN_ALL_TESTS(kArgc, const_cast<char**>(argv));
}

}  // namespace driver_unit_test
