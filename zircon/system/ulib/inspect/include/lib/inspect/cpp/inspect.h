// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_CPP_INSPECT_H_
#define LIB_INSPECT_CPP_INSPECT_H_

#include <lib/fit/result.h>
#include <lib/inspect/cpp/value_list.h>
#include <lib/inspect/cpp/vmo/state.h>
#include <lib/inspect/cpp/vmo/types.h>

#include <string>

namespace inspect {

// Settings to configure a specific Inspector.
struct InspectSettings final {
  // The maximum size of the created VMO, in bytes.
  //
  // The size must be non-zero, and it will be rounded up to the next page size.
  size_t maximum_size;
};

// The entry point into the Inspection API.
//
// An Inspector owns a particular tree of Inspect data.
class Inspector final {
 public:
  // Construct a new Inspector.
  Inspector();

  // Construct a new Inspector with the given settings.
  explicit Inspector(const InspectSettings& settings);

  // Construct a new Inspector backed by the given VMO.
  //
  // The VMO must support ZX_RIGHT_WRITE, ZX_VM_CAN_MAP_WRITE, and ZX_VM_CAN_MAP_READ permissions.
  //
  // If an invalid VMO is passed all Node operations will will have no effect.
  explicit Inspector(zx::vmo vmo);

  // Returns a duplicated read-only version of the VMO backing this inspector.
  zx::vmo DuplicateVmo() const;

  // Returns a copied version of the VMO backing this inspector.
  //
  // The returned copy will always be a consistent snapshot of the inspector state, truncated to
  // include only relevant pages from the underlying VMO.
  zx::vmo CopyVmo() const;

  // Returns a copy of the bytes of the VMO backing this inspector.
  //
  // The returned bytes will always be a consistent snapshot of the inspector state, truncated to
  // include only relevant bytes from the underlying VMO.
  std::vector<uint8_t> CopyBytes() const;

  // Returns a reference to the root node owned by this inspector.
  Node& GetRoot() const;

  // Boolean value of an Inspector is whether it is actually backed by a VMO.
  //
  // This method returns false if and only if Node operations on the Inspector are no-ops.
  explicit operator bool() { return state_ != nullptr; }

 private:
  // The root node for the Inspector.
  std::unique_ptr<Node> root_;

  // The internal state for this inspector.
  std::shared_ptr<internal::State> state_;
};

}  // namespace inspect

#endif  // LIB_INSPECT_CPP_INSPECT_H_
