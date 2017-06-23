// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_SOCKET_STRINGS_H_
#define LIB_MTL_SOCKET_STRINGS_H_

#include <mx/socket.h>

#include <string>

#include "lib/ftl/ftl_export.h"
#include "lib/ftl/strings/string_view.h"

namespace mtl {

// Copies the data from |source| into |contents| and returns true on success and
// false on error. In case of I/O error, |contents| holds the data that could
// be read from source before the error occurred.
FTL_EXPORT bool BlockingCopyToString(mx::socket source, std::string* contents);

FTL_EXPORT bool BlockingCopyFromString(ftl::StringView source,
                                       const mx::socket& destination);

// Copies the string |contents| to a temporary socket and returns the
// consumer handle.
FTL_EXPORT mx::socket WriteStringToSocket(ftl::StringView source);

}  // namespace mtl

#endif  // LIB_MTL_SOCKET_STRINGS_H_
