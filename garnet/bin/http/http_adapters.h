// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HTTP_HTTP_ADAPTERS_H_
#define GARNET_BIN_HTTP_HTTP_ADAPTERS_H_

#include <fuchsia/net/oldhttp/cpp/fidl.h>

namespace http {

// Creates a new http error object from a http error code.
::fuchsia::net::oldhttp::HttpErrorPtr MakeHttpError(int error_code);

}  // namespace http

#endif  // GARNET_BIN_HTTP_HTTP_ADAPTERS_H_
