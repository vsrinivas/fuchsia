// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_PUBLIC_CONSTANTS_H_
#define APPS_LEDGER_SRC_STORAGE_PUBLIC_CONSTANTS_H_

#include "lib/ftl/strings/string_view.h"

namespace storage {

// The size of a commit id in number of bytes.
constexpr unsigned long kCommitIdSize = 32;

// The size of an object id in number of bytes.
constexpr unsigned long kObjectIdSize = 32;

constexpr char kFirstPageCommitIdArray[kCommitIdSize] = {0};

constexpr const ftl::StringView kFirstPageCommitId(kFirstPageCommitIdArray,
                                                   kCommitIdSize);

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_PUBLIC_CONSTANTS_H_
