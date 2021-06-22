// Copyright (c) 2021 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "recovery_trigger.h"

namespace wlan::brcmfmac {

RecoveryTrigger::RecoveryTrigger(std::shared_ptr<std::function<zx_status_t()>> callback)
    : firmware_crash_(kFirmwareCrashThreshold, callback),
      sdio_timeout_(kSdioTimeoutThreshold, callback) {}

RecoveryTrigger::~RecoveryTrigger() = default;

void RecoveryTrigger::ClearStatistics() {
  BRCMF_INFO("The recovery process has been triggered, clearing all counters");
  firmware_crash_.Clear();
  sdio_timeout_.Clear();
}

}  // namespace wlan::brcmfmac
