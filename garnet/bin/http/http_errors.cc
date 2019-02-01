// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "http_errors.h"

#include "lib/fxl/logging.h"

namespace http {

std::string ErrorToString(int error) {
  return "http::" + ErrorToShortString(error);
}

std::string ErrorToShortString(int error) {
  if (error == 0)
    return "OK";

  const char* error_string;
  switch (error) {
#define HTTP_ERROR(label, value) \
  case HTTP_ERR_##label:         \
    error_string = #label;       \
    break;
#include "garnet/bin/http/http_error_list.h"
#undef HTTP_ERROR
    default:
      FXL_NOTREACHED();
      error_string = "<unknown>";
  }
  return std::string("HTTP_ERR_") + error_string;
}

}  // namespace http
