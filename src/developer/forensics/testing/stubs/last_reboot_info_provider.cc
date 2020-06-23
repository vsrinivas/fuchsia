// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/last_reboot_info_provider.h"

#include <lib/syslog/cpp/macros.h>

namespace forensics {
namespace stubs {

void LastRebootInfoProvider::Get(GetCallback callback) {
  FX_CHECK(!has_been_called_) << "Get() can only be called once";
  has_been_called_ = true;
  callback(std::move(last_reboot_));
}

}  // namespace stubs
}  // namespace forensics
