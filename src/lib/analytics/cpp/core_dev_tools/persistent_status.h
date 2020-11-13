// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_PERSISTENT_STATUS_H_
#define SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_PERSISTENT_STATUS_H_

#include <string>
#include <string_view>

namespace analytics::core_dev_tools::internal {

// Manages persistent statuses (states written to files) related to analytics, such as:
// - opt-in/out status of analytics collection
// - first-run status of a tool
// This class does not provide an in-memory cache for the statuses.
class PersistentStatus {
 public:
  explicit PersistentStatus(std::string_view tool_name);

  // Manage the persistent opt-in/out status. Will also manage other properties associated with the
  // enable/disable action, such as the UUID, as specified in the PDD of Fuchsia host tools.
  static void Enable();
  static void Disable();
  static bool IsEnabled();

  // Get UUID of the user
  static std::string GetUuid();

  // Indicates whether it is the very first launch of the first tool among core developer tools.
  static bool IsFirstLaunchOfFirstTool();

  // Manage the persistent status indicating whether a tool has been launched.
  void MarkAsDirectlyLaunched();
  bool IsFirstDirectLaunch() const;

 private:
  std::string tool_name_;
  std::string launched_property_;
};

}  // namespace analytics::core_dev_tools::internal

#endif  // SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_PERSISTENT_STATUS_H_
