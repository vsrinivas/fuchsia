// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_LAST_REBOOT_REBOOT_WATCHER_H_
#define SRC_DEVELOPER_FEEDBACK_LAST_REBOOT_REBOOT_WATCHER_H_

#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>

#include <string>

#include "src/developer/feedback/utils/cobalt/logger.h"

namespace feedback {

// Perists the graceful reason for a reboot so it can be recalled after the device has turned back
// on.
class ImminentGracefulRebootWatcher
    : public fuchsia::hardware::power::statecontrol::RebootMethodsWatcher {
 public:
  ImminentGracefulRebootWatcher(const std::string& path, cobalt::Logger* cobalt)
      : path_(path), cobalt_(cobalt) {}

  void OnReboot(fuchsia::hardware::power::statecontrol::RebootReason reason,
                OnRebootCallback callback) override;

 private:
  std::string path_;
  cobalt::Logger* cobalt_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_LAST_REBOOT_REBOOT_WATCHER_H_
