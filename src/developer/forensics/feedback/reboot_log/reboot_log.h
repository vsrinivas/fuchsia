// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_REBOOT_LOG_REBOOT_LOG_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_REBOOT_LOG_REBOOT_LOG_H_

#include <lib/zx/time.h>

#include <optional>
#include <string>

#include "src/developer/forensics/feedback/reboot_log/reboot_reason.h"

namespace forensics {
namespace feedback {

// Wrapper around a device's reboot log.
class RebootLog {
 public:
  static RebootLog ParseRebootLog(const std::string& zircon_reboot_log_path,
                                  const std::string& graceful_reboot_log_path, bool not_a_fdr);

  bool HasUptime() const { return last_boot_uptime_.has_value(); }

  std::string RebootLogStr() const { return reboot_log_str_; }
  enum RebootReason RebootReason() const { return reboot_reason_; }
  zx::duration Uptime() const { return last_boot_uptime_.value(); }

  // Exposed for testing purposes.
  RebootLog(enum RebootReason reboot_reason, std::string reboot_log_str,
            std::optional<zx::duration> last_boot_uptime);

 private:
  enum RebootReason reboot_reason_;
  std::string reboot_log_str_;
  std::optional<zx::duration> last_boot_uptime_;
};

}  // namespace feedback
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_REBOOT_LOG_REBOOT_LOG_H_
