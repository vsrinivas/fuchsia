// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "log_settings.h"
#include "macros.h"

namespace syslog_backend {

bool fx_log_compat_no_interest_listener() { return true; }
bool fx_log_compat_flush_record(LogBuffer* buffer) { return false; }

}  // namespace syslog_backend
