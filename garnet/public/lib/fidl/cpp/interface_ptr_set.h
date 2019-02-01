// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERFACE_PTR_SET_H_
#define LIB_FIDL_CPP_INTERFACE_PTR_SET_H_

#include <zircon/assert.h>
#include <vector>

#include "lib/fidl/cpp/interface_ptr.h"

namespace fidl {

// Contains a set of |InterfacePtr| objects, each with their own channels.
//
// An |InterfacePtr| is removed from the set and destroyed when its underlying
// channel experiences an error. When the set is destroyed, all the underlying
// channels are closed.
//
// An |InterfacePtrSet| is useful for broadcasting messages to a set of clients,
// each with their own implementation of |Interface|.
//
// See also:
//
//  * |BindingSet|, which is the server analog of an |InterfacePtrSet|.
template <typename Interface>
class InterfacePtrSet {
 public:
  using StorageType = std::vector<std::unique_ptr<InterfacePtr<Interface>>>;
  using Ptr = InterfacePtr<Interface>;

  // Creates an empty |InterfacePtrSet|.
  InterfacePtrSet() = default;

  InterfacePtrSet(const InterfacePtrSet& other) = delete;
  InterfacePtrSet& operator=(const InterfacePtrSet& other) = delete;

  // Adds the given |InterfacePtr| to the set.
  //
  // The |InterfacePtr| must already be bound to a channel. The |InterfacePtr|
  // will be removed from the set when its underlying channel experiences an
  // error.
  void AddInterfacePtr(InterfacePtr<Interface> ptr) {
    ZX_DEBUG_ASSERT(ptr.is_bound());
    // Allocated a new |InterfacePtr| so that we can have a unique value to use
    // as a key for removing the InterfacePtr. Otherwise, we'll lose track of
    // the InterfacePtr when the vector resizes.
    ptrs_.push_back(std::make_unique<Ptr>(std::move(ptr)));
    auto* pointer = ptrs_.back().get();
    pointer->set_error_handler(
        [pointer, this](zx_status_t status) { this->RemoveOnError(pointer); });
  }

  // The |InterfacePtr| objects stored in this set.
  //
  // This collection of bindings can be invalidated when an |InterfacePtr| in
  // the set encounters a connection error because connection errors causes the
  // |InterfacePtrSet| to remove the |InterfacePtr| from the set.
  const StorageType& ptrs() const { return ptrs_; }

  // Closes all channels associated with |InterfacePtr| objects in the set.
  //
  // After this method returns, the set is empty.
  void CloseAll() { ptrs_.clear(); }

  // The number of |InterfacePtr| objects in the set.
  //
  // This number might be smaller than the number of |InterfacePtr| objects
  // added to the set if some of the underlying channels have experienced an
  // error.
  size_t size() const { return ptrs_.size(); }

 private:
  // Removes the given |pointer| from the set.
  //
  // Called from the error handler callback for |pointer|.
  void RemoveOnError(Ptr* pointer) {
    auto it = std::find_if(ptrs_.begin(), ptrs_.end(),
                           [pointer](const std::unique_ptr<Ptr>& p) {
                             return p.get() == pointer;
                           });
    ZX_DEBUG_ASSERT(it != ptrs_.end());
    ptrs_.erase(it);
  }

  // We use |unique_ptr| rather than just |InterfacePtr| so that we can keep
  // track of the |InterfacePtr| objects after the |vector| resizes and moves
  // its contents to its new buffer.
  StorageType ptrs_;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERFACE_PTR_SET_H_
