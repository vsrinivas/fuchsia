// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_VMO_STRINGS_H_
#define SRC_LEDGER_LIB_VMO_STRINGS_H_

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/zx/vmo.h>

#include <string>

#include "src/ledger/lib/vmo/sized_vmo.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

// Make a new shared buffer with the contents of a string.
bool VmoFromString(const absl::string_view& string, SizedVmo* handle_ptr);

// Make a new shared buffer with the contents of a string.
bool VmoFromString(const absl::string_view& string, fuchsia::mem::Buffer* buffer_ptr);

// Copy the contents of a shared buffer into a string.
bool StringFromVmo(const SizedVmo& handle, std::string* string_ptr);

// Copy the contents of a shared buffer into a string.
bool StringFromVmo(const fuchsia::mem::Buffer& handle, std::string* string_ptr);

// Copy the contents of a shared buffer upto |num_bytes| into a string.
// |num_bytes| should be <= |handle.size|.
bool StringFromVmo(const fuchsia::mem::Buffer& handle, size_t num_bytes, std::string* string_ptr);

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_VMO_STRINGS_H_
