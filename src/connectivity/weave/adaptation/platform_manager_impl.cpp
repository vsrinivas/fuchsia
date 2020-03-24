// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/PlatformManager.h>

#include "generic_platform_manager_impl_fuchsia.ipp"
// clang-format on

namespace nl {
namespace Weave {
namespace DeviceLayer {

PlatformManagerImpl PlatformManagerImpl::sInstance;

WEAVE_ERROR PlatformManagerImpl::_InitWeaveStack(void) {
  return Internal::GenericPlatformManagerImpl_Fuchsia<PlatformManagerImpl>::_InitWeaveStack();
}

void PlatformManagerImpl::ShutdownWeaveStack(void) {
  Internal::GenericPlatformManagerImpl_Fuchsia<PlatformManagerImpl>::_ShutdownWeaveStack();
}

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
