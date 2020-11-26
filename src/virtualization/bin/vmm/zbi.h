// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ZBI_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ZBI_H_

#include <lib/zbitl/error_string.h>
#include <zircon/errors.h>

#include <string_view>

#include "sdk/lib/syslog/cpp/macros.h"

// A convenience function that that translates the common ZBI view result type
// to a zx_status_t, logging an error if present.
template <typename ZbitlResult>
zx_status_t LogIfZbiError(ZbitlResult&& result, std::string_view context = "") {
  if (result.is_ok()) {
    return ZX_OK;
  }
  if (context.empty()) {
    FX_LOGS(ERROR) << zbitl::ViewErrorString(result.error_value());
  } else {
    FX_LOGS(ERROR) << context << ": " << zbitl::ViewErrorString(result.error_value());
  }
  return ZX_ERR_INTERNAL;
}

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ZBI_H_
