// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crash_reports/info/crash_register_info.h"

#include <lib/syslog/cpp/macros.h>

namespace feedback {

CrashRegisterInfo::CrashRegisterInfo(std::shared_ptr<InfoContext> context)
    : context_(std::move(context)) {
  FX_CHECK(context_);
}

void CrashRegisterInfo::UpsertComponentToProductMapping(const std::string& component_url,
                                                        const Product& product) {
  context_->InspectManager().UpsertComponentToProductMapping(component_url, product);
}

}  // namespace feedback
