// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_CPP_READER_H_
#define LIB_INSPECT_CPP_READER_H_

#include <lib/fit/promise.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/vmo/snapshot.h>

namespace inspect {

// Construct a new hierarchy by synchronously reading objects out of
// the given VMO.
fit::result<Hierarchy> ReadFromVmo(const zx::vmo& vmo);

// Construct a new hierarchy by synchronously reading objects out of the
// given VMO Snapshot.
fit::result<Hierarchy> ReadFromSnapshot(Snapshot snapshot);

// Construct a new hierarchy by synchronously reading objects out of the
// contents of the given buffer.
fit::result<Hierarchy> ReadFromBuffer(std::vector<uint8_t> buffer);

}  // namespace inspect

#endif  // LIB_INSPECT_CPP_READER_H_
