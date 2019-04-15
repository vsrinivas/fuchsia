// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/constants.h"

#include <fuchsia/ledger/internal/cpp/fidl.h>

#include <string>

namespace ledger {

namespace {
const char kNullPageId[::fuchsia::ledger::kPageIdSize] = {};
}  // namespace

// The zero-initialized root id.
constexpr fxl::StringView kRootPageId(kNullPageId,
                                      ::fuchsia::ledger::kPageIdSize);

}  // namespace ledger
