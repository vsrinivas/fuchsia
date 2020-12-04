// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_UTIL_TESTS_MOCKS_MOCK_BOOT_INFO_MANAGER_H_
#define SRC_UI_A11Y_LIB_UTIL_TESTS_MOCKS_MOCK_BOOT_INFO_MANAGER_H_

#include "src/ui/a11y/lib/util/boot_info_manager.h"

namespace accessibility_test {

class MockBootInfoManager : public a11y::BootInfoManager {
 public:
  MockBootInfoManager(sys::ComponentContext* context, bool last_reboot_was_user_initiated)
      : a11y::BootInfoManager(context),
        last_reboot_was_user_initiated_(last_reboot_was_user_initiated) {}
  ~MockBootInfoManager() override = default;

  bool LastRebootWasUserInitiated() override { return last_reboot_was_user_initiated_; }

  void SetLastRebootWasUserInitiated(bool last_reboot_was_user_initiated) {
    last_reboot_was_user_initiated_ = last_reboot_was_user_initiated;
  }

 private:
  bool last_reboot_was_user_initiated_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_UTIL_TESTS_MOCKS_MOCK_BOOT_INFO_MANAGER_H_
