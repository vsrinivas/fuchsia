// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/camera/common/llcpp/fidl.h>

namespace virtual_camera {

class VirtualCameraFactory;

using VirtualCameraFactoryType = ddk::Device<VirtualCameraFactory, ddk::Unbindable>;

class VirtualCameraFactory : public VirtualCameraFactoryType,
                             public llcpp::fuchsia::camera::common::VirtualCameraFactory::Interface,
                             public ddk::EmptyProtocol<ZX_PROTOCOL_VCAM_FACTORY> {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(VirtualCameraFactory);

    static zx_status_t Create(void* ctx, zx_device_t* parent);

    VirtualCameraFactory(zx_device_t* parent)
        : VirtualCameraFactoryType(parent) {}

    ~VirtualCameraFactory() {}

    void DdkRelease();

    void DdkUnbind();

    void CreateDevice(
        ::llcpp::fuchsia::camera::common::VirtualCameraConfig config,
        ::llcpp::fuchsia::camera::common::VirtualCameraFactory::Interface::CreateDeviceCompleter::Sync completer);
};

} // namespace virtual_camera
