// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_VMO_READER_H_
#define LIB_INSPECT_VMO_READER_H_

#include <lib/inspect-vmo/snapshot.h>
#include <lib/inspect/reader.h>

namespace inspect {
namespace vmo {
namespace reader {

// Read the contents of a |Shapshot| into an |ObjectHierarchy|.
std::unique_ptr<ObjectHierarchy> ReadSnapshot(Snapshot snapshot);

}  // namespace reader
}  // namespace vmo
}  // namespace inspect

#endif  // LIB_INSPECT_VMO_READER_H_
