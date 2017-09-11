// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net_errors.h"

#include "lib/fxl/logging.h"

namespace network {

std::string ErrorToString(int error) {
  return "network::" + ErrorToShortString(error);
}

std::string ErrorToShortString(int error) {
  if (error == 0)
    return "OK";

  const char* error_string;
  switch (error) {
#define NET_ERROR(label, value) \
  case NETWORK_ERR_##label:     \
    error_string = #label;      \
    break;
#include "apps/network/net_error_list.h"
#undef NET_ERROR
    default:
      FXL_NOTREACHED();
      error_string = "<unknown>";
  }
  return std::string("NETWORK_ERR_") + error_string;
}

}  // namespace network
