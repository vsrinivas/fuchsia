// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/ConnectivityManager.h>
#include <Warm/Warm.h>
// clang-format on

using namespace ::nl::Weave::DeviceLayer;
using namespace ::nl::Weave::DeviceLayer::Internal;

using namespace ::nl;
using namespace ::nl::Weave;
using namespace ::nl::Weave::Warm;

// ==================== WARM Platform Functions ====================

namespace nl {
namespace Weave {
namespace Warm {
namespace Platform {

WEAVE_ERROR Init(WarmFabricStateDelegate *inFabricStateDelegate) { return WEAVE_NO_ERROR; }

void CriticalSectionEnter(void) {}

void CriticalSectionExit(void) {}

void RequestInvokeActions(void) {}

PlatformResult AddRemoveHostAddress(InterfaceType inInterfaceType, const Inet::IPAddress &inAddress,
                                    uint8_t inPrefixLength, bool inAdd) {
  return kPlatformResultSuccess;
}

PlatformResult AddRemoveHostRoute(InterfaceType inInterfaceType, const Inet::IPPrefix &inPrefix,
                                  RoutePriority inPriority, bool inAdd) {
  return kPlatformResultSuccess;
}

#if WARM_CONFIG_SUPPORT_THREAD

PlatformResult AddRemoveThreadAddress(InterfaceType inInterfaceType,
                                      const Inet::IPAddress &inAddress, bool inAdd) {
  return kPlatformResultSuccess;
}

#endif  // WARM_CONFIG_SUPPORT_THREAD

#if WARM_CONFIG_SUPPORT_THREAD_ROUTING

#error "Weave Thread router support not implemented"

PlatformResult StartStopThreadAdvertisement(InterfaceType inInterfaceType,
                                            const Inet::IPPrefix &inPrefix, bool inStart) {
  // TODO: implement me
}

#endif  // WARM_CONFIG_SUPPORT_THREAD_ROUTING

#if WARM_CONFIG_SUPPORT_BORDER_ROUTING

#error "Weave border router support not implemented"

PlatformResult AddRemoveThreadRoute(InterfaceType inInterfaceType, const Inet::IPPrefix &inPrefix,
                                    RoutePriority inPriority, bool inAdd) {
  // TODO: implement me
}

PlatformResult SetThreadRoutePriority(InterfaceType inInterfaceType, const Inet::IPPrefix &inPrefix,
                                      RoutePriority inPriority) {
  // TODO: implement me
}

#endif  // WARM_CONFIG_SUPPORT_BORDER_ROUTING

}  // namespace Platform
}  // namespace Warm
}  // namespace Weave
}  // namespace nl
