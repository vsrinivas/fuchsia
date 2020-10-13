// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FVM_FVM_H_
#define FVM_FVM_H_

#include <zircon/types.h>

#include <optional>

#include <fvm/format.h>

namespace fvm {

// Update's the metadata's hash field to accurately reflect the contents of metadata.
void UpdateHash(void* metadata, size_t metadata_size);

// Validate the FVM header information, and identify which copy of metadata (primary or backup)
// should be used for initial reading, if either.
//
// The two copies of the metadata block from the beginning of the device is passed in, along with
// their length (they should be the same size). These blocks should include both primary and
// secondary copies of the metadata.
//
// On success, the superblock type which is valid is returned. If both copies are invalid, a null
// optional is returned.
// TODO(jfsulliv): Remove this once all uses are ported to Metadata.
std::optional<SuperblockType> ValidateHeader(const void* primary_metadata,
                                             const void* secondary_metadata, size_t metadata_size);

}  // namespace fvm

#endif  // FVM_FVM_H_
