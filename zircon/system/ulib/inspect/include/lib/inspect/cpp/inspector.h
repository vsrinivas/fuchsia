// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_CPP_INSPECTOR_H_
#define LIB_INSPECT_CPP_INSPECTOR_H_

#include <lib/fit/optional.h>
#include <lib/fit/result.h>
#include <lib/inspect/cpp/value_list.h>
#include <lib/zx/vmo.h>

#include <string>

namespace inspect {

class Inspector;
class Node;

namespace internal {
class State;

// Internal accessor for obtaining fields from an Inspector.
std::shared_ptr<State> GetState(const Inspector* inspector);
}  // namespace internal

// Settings to configure a specific Inspector.
struct InspectSettings final {
  // The maximum size of the created VMO, in bytes.
  //
  // The size must be non-zero, and it will be rounded up to the next page size.
  size_t maximum_size;
};

// The entry point into the Inspection API.
//
// An Inspector wraps a particular tree of Inspect data.
//
// This class is thread safe and copyable.
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

  // Emplace a value to be owned by this Inspector.
  template <typename T>
  void emplace(T value) {
    value_list_->emplace(std::move(value));
  }

  // Gets the names of the inspectors linked off of this inspector.
  std::vector<std::string> GetChildNames() const;

  // Open a child of this inspector by name.
  //
  // Returns a promise for the opened inspector.
  fit::promise<Inspector> OpenChild(const std::string& name) const;

 private:
  friend std::shared_ptr<internal::State> internal::GetState(const Inspector* inspector);

  // The root node for the Inspector.
  //
  // Shared pointers are used so Inspector is copyable.
  std::shared_ptr<Node> root_;

  // The internal state for this inspector.
  //
  // Shared pointers are used so Inspector is copyable.
  std::shared_ptr<internal::State> state_;

  // Internally stored values owned by this Inspector.
  //
  // Shared pointers are used so Inspector is copyable.
  std::shared_ptr<ValueList> value_list_;
};

// Generate a unique name with the given prefix.
std::string UniqueName(const std::string& prefix);

}  // namespace inspect

#endif  // LIB_INSPECT_CPP_INSPECTOR_H_
