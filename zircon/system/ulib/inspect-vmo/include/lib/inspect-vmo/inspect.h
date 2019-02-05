// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_VMO_INSPECT_H_
#define LIB_INSPECT_VMO_INSPECT_H_

#include <lib/inspect-vmo/state.h>

namespace inspect {
namespace vmo {

// Entry point into the Inspection API.
//
// The inspector owns a VMO into which inspection data is written for
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

    // Return a read-only clone of the VMO stored by this inspector. This
    // may be passed to other processes for inspection.
    zx::vmo GetReadOnlyVmoClone() const { return state_->GetReadOnlyVmoClone(); }

    // Return a reference to the root object in this inspector. Operations
    // on this object will be reflected in the VMO.
    Object& GetRootObject() { return root_object_; }

private:
    // Shared reference to the state, which owns the VMO.
    fbl::RefPtr<internal::State> state_;

    // Root object stored in the state. All objects and values exist
    // under this object.
    Object root_object_;
};

} // namespace vmo
} // namespace inspect

#endif  // LIB_INSPECT_VMO_INSPECT_H_
