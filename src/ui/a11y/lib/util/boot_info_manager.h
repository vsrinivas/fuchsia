// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_UTIL_BOOT_INFO_MANAGER_H_
#define SRC_UI_A11Y_LIB_UTIL_BOOT_INFO_MANAGER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

namespace a11y {

// This class is used to gather accessibility-relevant information about boots
// from system sources.
class BootInfoManager {
 public:
  explicit BootInfoManager(sys::ComponentContext* context);
  virtual ~BootInfoManager() = default;

  // Returns true if the most recent reboot was user initiated, and false
  // otherwise.
  virtual bool LastRebootWasUserInitiated();

 private:
  fuchsia::feedback::LastRebootInfoProviderSyncPtr last_reboot_info_provider_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_UTIL_BOOT_INFO_MANAGER_H_
