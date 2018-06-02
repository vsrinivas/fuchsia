// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HTTP_HTTP_ERRORS_H_
#define GARNET_BIN_HTTP_HTTP_ERRORS_H_

#include <string>

namespace http {

enum Error {
  // No error.
  OK = 0,

#define HTTP_ERROR(label, value) HTTP_ERR_##label = value,
#include "garnet/bin/http/http_error_list.h"
#undef HTTP_ERROR
};

// Returns a textual representation of the error code for logging purposes.
std::string ErrorToString(int error);

// Same as above, but leaves off the leading "http::".
std::string ErrorToShortString(int error);

}  // namespace http

#endif  // GARNET_BIN_HTTP_HTTP_ERRORS_H_
