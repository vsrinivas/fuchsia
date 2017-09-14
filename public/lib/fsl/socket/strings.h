// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_SOCKET_STRINGS_H_
#define LIB_FSL_SOCKET_STRINGS_H_

#include <zx/socket.h>

#include <string>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/strings/string_view.h"

namespace fsl {

// Copies the data from |source| into |contents| and returns true on success and
// false on error. In case of I/O error, |contents| holds the data that could
// be read from source before the error occurred.
FXL_EXPORT bool BlockingCopyToString(zx::socket source, std::string* contents);

FXL_EXPORT bool BlockingCopyFromString(fxl::StringView source,
                                       const zx::socket& destination);

// Copies the string |contents| to a temporary socket and returns the
// consumer handle.
FXL_EXPORT zx::socket WriteStringToSocket(fxl::StringView source);

}  // namespace fsl

#endif  // LIB_FSL_SOCKET_STRINGS_H_
