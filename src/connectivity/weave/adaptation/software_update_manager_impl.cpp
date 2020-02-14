// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>

#if WEAVE_DEVICE_CONFIG_ENABLE_SOFTWARE_UPDATE_MANAGER

#include <Weave/Profiles/WeaveProfiles.h>
#include <Weave/Profiles/common/CommonProfile.h>

#include <Weave/DeviceLayer/internal/GenericSoftwareUpdateManagerImpl.ipp>
#include <Weave/DeviceLayer/internal/GenericSoftwareUpdateManagerImpl_BDX.ipp>
// clang-format on

namespace nl {
namespace Weave {
namespace DeviceLayer {

SoftwareUpdateManagerImpl SoftwareUpdateManagerImpl::sInstance;

WEAVE_ERROR SoftwareUpdateManagerImpl::_Init(void) { return WEAVE_NO_ERROR; }

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // WEAVE_DEVICE_CONFIG_ENABLE_SOFTWARE_UPDATE_MANAGER
