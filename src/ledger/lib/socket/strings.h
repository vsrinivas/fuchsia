// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_SOCKET_STRINGS_H_
#define SRC_LEDGER_LIB_SOCKET_STRINGS_H_

#include <lib/zx/socket.h>

#include <string>

#include "src/lib/fxl/strings/string_view.h"

namespace ledger {

// Copies the data from |source| into |contents| and returns true on success and
// false on error. In case of I/O error, |contents| holds the data that could
// be read from source before the error occurred.
bool BlockingCopyToString(zx::socket source, std::string* contents);

bool BlockingCopyFromString(fxl::StringView source, const zx::socket& destination);

// Copies the string |source| to a temporary socket and returns the
// consumer handle.
zx::socket WriteStringToSocket(fxl::StringView source);

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_SOCKET_STRINGS_H_
