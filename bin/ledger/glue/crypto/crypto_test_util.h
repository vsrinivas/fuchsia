// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_GLUE_CRYPTO_CRYPTO_TEST_UTIL_H_
#define APPS_LEDGER_SRC_GLUE_CRYPTO_CRYPTO_TEST_UTIL_H_

#include <string>

#include "lib/fxl/strings/string_view.h"

namespace glue {
std::string FromHex(fxl::StringView data);
}

#endif  // APPS_LEDGER_SRC_GLUE_CRYPTO_CRYPTO_TEST_UTIL_H_
