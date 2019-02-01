// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/helper/zx_status.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace debug_ipc {

std::string ZxStatusToString(uint32_t status) {
  const char* status_name = nullptr;
  switch (status) {
    case kZxOK:
      status_name = "ZX_OK";
      break;
    case kZxErrInternal:
      status_name = "ZX_ERR_INTERNAL";
      break;
    case kZxErrNotSupported:
      status_name = "ZX_ERR_NOT_SUPPORTED";
      break;
    case kZxErrInvalidArgs:
      status_name = "ZX_ERR_INVALID_ARGS";
      break;
    case kZxErrNoResources:
      status_name = "ZX_ERR_NO_RESOURCES";
      break;
    case kZxErrOutOfRange:
      status_name = "ZX_ERR_OUT_OF_RANGE";
      break;
    case kZxErrIO:
      status_name = "ZX_ERR_IO";
      break;
    default:
      status_name = "<unsupported status>";
      break;
  }
  FXL_DCHECK(status_name);
  return fxl::StringPrintf("%s (%d)", status_name, status);
}

}  // namespace debug_ipc
