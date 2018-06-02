// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/http/http_adapters.h"
#include "garnet/bin/http/http_errors.h"

namespace http {

namespace oldhttp = ::fuchsia::net::oldhttp;

oldhttp::HttpErrorPtr MakeHttpError(int error_code) {
  oldhttp::HttpErrorPtr error = oldhttp::HttpError::New();
  error->code = error_code;
  if (error_code <= 0)
    error->description = ErrorToString(error_code);
  return error;
}

}  // namespace http
