// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/insntrace/utils.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace insntrace {

void LogFidlFailure(const char* rqst_name, zx_status_t fidl_status, zx_status_t rqst_status) {
  if (fidl_status != ZX_OK) {
    FX_LOGS(ERROR) << rqst_name << " (FIDL) failed: status=" << fidl_status << "/"
                   << zx_status_get_string(fidl_status);
  } else if (rqst_status != ZX_OK) {
    FX_LOGS(ERROR) << rqst_name << " failed: error=" << rqst_status << "/"
                   << zx_status_get_string(rqst_status);
  }
}

}  // namespace insntrace
