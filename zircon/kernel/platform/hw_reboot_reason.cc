// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <platform.h>
#include <zircon/boot/image.h>

namespace {
zbi_hw_reboot_reason_t g_platform_hw_reboot_reason = ZbiHwRebootReason::Undefined;
}

void platform_set_hw_reboot_reason(zbi_hw_reboot_reason_t reason) {
  g_platform_hw_reboot_reason = reason;
}

zbi_hw_reboot_reason_t platform_hw_reboot_reason(void) { return g_platform_hw_reboot_reason; }
