// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/util/boot_info_manager.h"

#include <lib/syslog/cpp/macros.h>

namespace a11y {

BootInfoManager::BootInfoManager(sys::ComponentContext* context) {
  FX_CHECK(context);
  FX_CHECK(context->svc()->Connect(last_reboot_info_provider_.NewRequest()) == ZX_OK);
}

bool BootInfoManager::LastRebootWasUserInitiated() {
  fuchsia::feedback::LastReboot last_reboot;
  auto status = last_reboot_info_provider_->Get(&last_reboot);
  FX_DCHECK(status == ZX_OK);
  return last_reboot.has_reason() &&
         last_reboot.reason() == fuchsia::feedback::RebootReason::USER_REQUEST;
}

}  // namespace a11y
