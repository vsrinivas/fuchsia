// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_WEAVE_DEVICE_PLATFORM_EVENT_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_WEAVE_DEVICE_PLATFORM_EVENT_H_

#pragma GCC diagnostic push
#include <Weave/DeviceLayer/WeaveDeviceEvent.h>
#pragma GCC diagnostic pop

namespace nl {
namespace Weave {
namespace DeviceLayer {

enum WeaveDevicePlatformEventType {
  kShutdownRequest = DeviceEventType::kRange_PublicPlatformSpecific,
};

/**
 * Represents platform-specific event information for the Fuchsia platform.
 */
struct WeaveDevicePlatformEvent final {
  // Currently |arg| is used by tests, to verify if the events were
  // successfully received from higher layers. It can be used for any useful data.
  void *arg;
};

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_WEAVE_DEVICE_PLATFORM_EVENT_H_
