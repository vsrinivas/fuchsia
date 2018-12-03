// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_CONSTANTS_H_
#define PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_CONSTANTS_H_

#include <stdint.h>

#include <lib/fxl/strings/string_view.h>

namespace storage {

// The size of a commit id in number of bytes.
inline constexpr uint64_t kCommitIdSize = 32;

// The ID of the first commit of a page.
extern const fxl::StringView kFirstPageCommitId;
}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_CONSTANTS_H_
