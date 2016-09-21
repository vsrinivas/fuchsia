// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net_errors.h"

#include "lib/ftl/logging.h"

namespace net {

std::string ErrorToString(int error) {
  return "net::" + ErrorToShortString(error);
}

std::string ErrorToShortString(int error) {
  if (error == 0)
    return "OK";

  const char* error_string;
  switch (error) {
#define NET_ERROR(label, value) \
  case ERR_ ## label: \
    error_string = # label; \
    break;
#include "apps/network/net_error_list.h"
#undef NET_ERROR
  default:
    FTL_NOTREACHED();
    error_string = "<unknown>";
  }
  return std::string("ERR_") + error_string;
}

}  // namespace net
