// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fuchsia/camera/test/c/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <memory>
#include <zircon/fidl.h>

namespace camera {
// |ArmIspDeviceTester| is spawned by the driver in |arm-isp.cpp|
// This provides the interface provided in fuchsia-camera-test/isp.fidl in Zircon.

class ArmIspDevice;

class ArmIspDeviceTester;
using IspDeviceTesterType = ddk::Device<ArmIspDeviceTester, ddk::Unbindable, ddk::Messageable>;

class ArmIspDeviceTester : public IspDeviceTesterType,
                           public ddk::EmptyProtocol<ZX_PROTOCOL_ISP_TEST> {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ArmIspDeviceTester);

    explicit ArmIspDeviceTester(ArmIspDevice* isp, zx_device_t* parent)
        : IspDeviceTesterType(parent), isp_(isp) {}

    static zx_status_t Create(ArmIspDevice* isp);

    // Methods required by the ddk.
    void DdkRelease();
    void DdkUnbind();
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

private:
    // DDKMessage Helper Functions.
    zx_status_t RunTests(fidl_txn_t* txn);

    static constexpr fuchsia_camera_test_IspTester_ops isp_tester_ops = {
        .RunTests = fidl::Binder<ArmIspDeviceTester>::BindMember<&ArmIspDeviceTester::RunTests>,
    };

    // The ArmIspDevice is a parent of the ArmIspDeviceTester, so it is guarenteed to exist as
    // long as the ArmIspDeviceTester is bound.
    ArmIspDevice* isp_;
};

} // namespace camera
