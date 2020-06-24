// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_LAST_REBOOT_REBOOT_WATCHER_H_
#define SRC_DEVELOPER_FORENSICS_LAST_REBOOT_REBOOT_WATCHER_H_

#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/service_directory.h>

#include <string>

#include "src/developer/forensics/utils/cobalt/logger.h"

namespace forensics {
namespace last_reboot {

// Perists the graceful reason for a reboot so it can be recalled after the device has turned back
// on.
class ImminentGracefulRebootWatcher
    : public fuchsia::hardware::power::statecontrol::RebootMethodsWatcher {
 public:
  ImminentGracefulRebootWatcher(std::shared_ptr<sys::ServiceDirectory> services,
                                const std::string& path, cobalt::Logger* cobalt);

  void Connect();

  void OnReboot(fuchsia::hardware::power::statecontrol::RebootReason reason,
                OnRebootCallback callback) override;

 private:
  std::shared_ptr<sys::ServiceDirectory> services_;
  std::string path_;
  cobalt::Logger* cobalt_;

  ::fidl::Binding<fuchsia::hardware::power::statecontrol::RebootMethodsWatcher> connection_;
};

}  // namespace last_reboot
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_LAST_REBOOT_REBOOT_WATCHER_H_
