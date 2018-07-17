// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_USER_SHELL_SETTINGS_USER_SHELL_SETTINGS_H_
#define PERIDOT_LIB_USER_SHELL_SETTINGS_USER_SHELL_SETTINGS_H_

#include <vector>

#include <fuchsia/ui/policy/cpp/fidl.h>

namespace modular {

// A data structure representing User Shell Settings. See
// |kDeviceShellConfigJsonPath| in user_shell_settings.cc for the path name and
// JSON that this is intended to represent.
struct UserShellSettings {
  // Returns the user shell settings for the system. This is guaranteed to be
  // O(1). This is thread-unsafe; callers can safely call this method if callers
  // synchronize access.
  static const std::vector<UserShellSettings>& GetSystemSettings();

  // The name of the user shell, e.g. "ermine".
  const std::string name;

  // The screen width & height in millimeters for the user shell's display.
  // Defaults to a signaling NaN so that any attempts to use it without checking
  // for NaN will trap.
  const float screen_width = std::numeric_limits<float>::signaling_NaN();
  const float screen_height = std::numeric_limits<float>::signaling_NaN();

  // The display usage policy for this user shell.
  const fuchsia::ui::policy::DisplayUsage display_usage =
      fuchsia::ui::policy::DisplayUsage::kUnknown;
};

bool operator==(const UserShellSettings& lhs, const UserShellSettings& rhs);

}  // namespace modular

#endif  // PERIDOT_LIB_USER_SHELL_SETTINGS_USER_SHELL_SETTINGS_H_
