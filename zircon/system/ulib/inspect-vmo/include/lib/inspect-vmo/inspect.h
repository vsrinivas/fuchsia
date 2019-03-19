// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_VMO_INSPECT_H_
#define LIB_INSPECT_VMO_INSPECT_H_

#include <lib/inspect-vmo/state.h>

namespace inspect {
namespace vmo {

// Entry point into the Inspection VMO.
//
// This inspector owns a VMO into which inspection data is written for
// later reading through a read-only copy of the VMO.
class Inspector final {
public:
    // Create a new inspection VMO with default capacity and maximum size.
    Inspector();

    // Create a new inspection VMO with explicit capacity and maximum size.
    Inspector(size_t capacity, size_t max_size);

    // Disallow copy and move.
    Inspector(const Inspector&) = delete;
    Inspector(Inspector&&) = default;
    Inspector& operator=(const Inspector&) = delete;
    Inspector& operator=(Inspector&&) = default;

    // Return a reference to the contained VMO. This VMO may be duplicated
    // and passed to reader processes for inspection.
    const zx::vmo& GetVmo() const { return state_->GetVmo(); }

    // Creates a new object stored at the root of the given VMO.
    // By convention, the object returned by the first call of this method is the root of the tree.
    // Objects created by additional calls may be ignored depending on the reader.
    Object CreateObject(const char* name) const;

private:
    // Shared reference to the state, which owns the VMO.
    fbl::RefPtr<internal::State> state_;
};

} // namespace vmo
} // namespace inspect

#endif // LIB_INSPECT_VMO_INSPECT_H_
