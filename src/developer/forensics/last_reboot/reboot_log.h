// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_LAST_REBOOT_REBOOT_LOG_H_
#define SRC_DEVELOPER_FORENSICS_LAST_REBOOT_REBOOT_LOG_H_

#include <lib/zx/time.h>

#include <optional>
#include <string>

#include "src/developer/forensics/last_reboot/reboot_reason.h"

namespace forensics {
namespace last_reboot {

// Wrapper around a device's reboot log.
class RebootLog {
 public:
  static RebootLog ParseRebootLog(const std::string& zircon_reboot_log_path,
                                  const std::string& graceful_reboot_log_path,
                                  const std::string& not_a_fdr_path);

  bool HasRebootLogStr() const { return reboot_log_str_.has_value(); }
  bool HasUptime() const { return last_boot_uptime_.has_value(); }

  std::string RebootLogStr() const { return reboot_log_str_.value(); }
  enum RebootReason RebootReason() const { return reboot_reason_; }
  zx::duration Uptime() const { return last_boot_uptime_.value(); }

  // Exposed for testing purposes.
  RebootLog(enum RebootReason reboot_reason, std::optional<std::string> reboot_log_str,
            std::optional<zx::duration> last_boot_uptime);

 private:
  enum RebootReason reboot_reason_;
  std::optional<std::string> reboot_log_str_;
  std::optional<zx::duration> last_boot_uptime_;
};

}  // namespace last_reboot
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_LAST_REBOOT_REBOOT_LOG_H_
