// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/public/constants.h"

#include "src/lib/fxl/strings/string_view.h"

namespace storage {

namespace {
constexpr char kFirstPageCommitIdArray[kCommitIdSize] = {};
}  // namespace

constexpr fxl::StringView kFirstPageCommitId(kFirstPageCommitIdArray,
                                             kCommitIdSize);
}  // namespace storage
