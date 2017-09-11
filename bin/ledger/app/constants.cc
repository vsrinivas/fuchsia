// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/constants.h"

#include <string>

namespace ledger {

namespace {
const char kNullPageId[kPageIdSize] = {};
}  // namespace

// The zero-initialized root id.
constexpr fxl::StringView kRootPageId(kNullPageId, kPageIdSize);

}  // namespace ledger
