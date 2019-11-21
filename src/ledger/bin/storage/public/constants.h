// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_PUBLIC_CONSTANTS_H_
#define SRC_LEDGER_BIN_STORAGE_PUBLIC_CONSTANTS_H_

#include <stdint.h>

#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

// The size of a commit id in number of bytes.
inline constexpr uint64_t kCommitIdSize = 32;

// The ID of the first commit of a page.
extern const absl::string_view kFirstPageCommitId;

// The size of a device id in number of bytes.
inline constexpr uint64_t kDeviceIdSize = 32;

// Maximum number of concurrent piece downloads for an opened page.
inline constexpr size_t kMaxConcurrentDownloads = 5;

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_PUBLIC_CONSTANTS_H_
