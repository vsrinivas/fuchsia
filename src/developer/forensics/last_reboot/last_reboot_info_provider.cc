// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/last_reboot/last_reboot_info_provider.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/feedback/reboot_log/reboot_reason.h"

namespace forensics {
namespace last_reboot {

LastRebootInfoProvider::LastRebootInfoProvider(const feedback::RebootLog& reboot_log) {
  if (reboot_log.Uptime().has_value()) {
    last_reboot_.set_uptime(reboot_log.Uptime()->get());
  }

  if (const auto graceful = OptionallyGraceful(reboot_log.RebootReason()); graceful.has_value()) {
    last_reboot_.set_graceful(graceful.value());
  }

  if (const auto fidl_reboot_reason = ToFidlRebootReason(reboot_log.RebootReason());
      fidl_reboot_reason.has_value()) {
    last_reboot_.set_reason(fidl_reboot_reason.value());
  }
}

void LastRebootInfoProvider::Get(GetCallback callback) {
  fuchsia::feedback::LastReboot last_reboot;

  if (const auto status = last_reboot_.Clone(&last_reboot); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Error cloning |last_reboot_|";
  }

  callback(std::move(last_reboot));
}

}  // namespace last_reboot
}  // namespace forensics
