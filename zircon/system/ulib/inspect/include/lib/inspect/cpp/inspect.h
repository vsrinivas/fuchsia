// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_CPP_INSPECT_H_
#define LIB_INSPECT_CPP_INSPECT_H_

#include <lib/fit/result.h>
#include <lib/inspect/cpp/vmo/state.h>
#include <lib/inspect/cpp/vmo/types.h>

#include <string>

namespace inspect {

// Settings to configure a specific Inspector.
struct InspectSettings {
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
  // Construct a new Inspector with the given name and default settings.
  explicit Inspector(const std::string& name);

  // Construct a new Inspector with the given name and settings.
  Inspector(const std::string& name, const InspectSettings& settings);

  // Construct a new Inspector with the given name backed by the given VMO.
  //
  // The VMO must support ZX_RIGHT_WRITE, ZX_VM_CAN_MAP_WRITE, and ZX_VM_CAN_MAP_READ permissions.
  //
  // If an invalid VMO is passed all Node operations will will have no effect.
  Inspector(const std::string& name, zx::vmo vmo);

  // Gets a reference to the VMO backing this inspector's tree if one exists, otherwise return
  // fit::error.
  fit::result<const zx::vmo*> GetVmo() const;

  // Gets a reference to the root node owned by this inspector.
  Node& GetRoot() const;

  // Takes the root node from this inspector.
  // Future calls for GetRoot or TakeRoot will return a separate no-op node, but will not crash.
  // DEPRECATED: This function will be removed, do not depend on being able to take the root.
  Node TakeRoot();

  // Boolean value of an Inspector is whether it is actually backed by a VMO.
  //
  // This method returns false if and only if Node operations on the Inspector are no-ops.
  explicit operator bool() { return state_ != nullptr; }

 private:
  // The root node for the Inspector.
  std::unique_ptr<Node> root_;

  // The internal state for this inspector.
  std::shared_ptr<State> state_;
};

// Generate a unique name with the given prefix.
std::string UniqueName(const std::string& prefix);

}  // namespace inspect

#endif  // LIB_INSPECT_CPP_INSPECT_H_
