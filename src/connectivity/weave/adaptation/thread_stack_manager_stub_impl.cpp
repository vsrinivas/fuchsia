// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/internal/DeviceNetworkInfo.h>
#include <Weave/DeviceLayer/PlatformManager.h>
#include <Weave/DeviceLayer/ThreadStackManager.h>
#include <Weave/Support/TraitEventUtils.h>
#include <Warm/Warm.h>
// clang-format on

#include "thread_stack_manager_stub_impl.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {

WEAVE_ERROR ThreadStackManagerStubImpl::GetPrimary802154MACAddress(uint8_t* mac_address) {
  // It is necessary to ensure that callers don't believe a real physical radio
  // is serviced by this stub implementation, so that they can treat all Thread
  // network management operations as 'best-effort'.
  return WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE;
}

nl::Weave::Profiles::DataManagement::event_id_t
ThreadStackManagerStubImpl::LogNetworkWpanStatsEvent(
    Schema::Nest::Trait::Network::TelemetryNetworkWpanTrait::NetworkWpanStatsEvent* event) {
  return nl::LogEvent(event);
}

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
