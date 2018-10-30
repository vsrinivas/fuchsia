// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_THREAD_SAFE_BINDING_SET_H_
#define LIB_FIDL_CPP_THREAD_SAFE_BINDING_SET_H_

#include <algorithm>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <lib/async/dispatcher.h>
#include <zircon/compiler.h>

#include "lib/fidl/cpp/binding.h"

namespace fidl {

// Manages a set of bindings to implemenations owned by the bound channels.
//
// The implementation pointer type of the binding is also parameterized,
// allowing the use of smart pointer types such as |std::unique_ptr<>| to
// reference the implementation.
//
// This class is thread-safe; bindings may be added or cleared from any thread.
//
// See also:
//
//  * |BindingSet|, which is the thread-hostile analog that offers more
//    functionality.
//  * |InterfacePtrSet|, which is the client analog of |BindingSet|.
template <typename Interface, typename ImplPtr = Interface*>
class ThreadSafeBindingSet {
 public:
  using Binding = ::fidl::Binding<Interface, ImplPtr>;
  using StorageType = std::vector<std::unique_ptr<Binding>>;

  ThreadSafeBindingSet() = default;

  ThreadSafeBindingSet(const ThreadSafeBindingSet&) = delete;
  ThreadSafeBindingSet& operator=(const ThreadSafeBindingSet&) = delete;

  // Adds a binding to the set.
  //
  // The given |ImplPtr| is bound to the channel underlying the
  // |InterfaceRequest|. The binding is removed (and the |~ImplPtr| called)
  // when the created binding has an error (e.g., if the remote endpoint of
  // the channel sends an invalid message).
  //
  // Whether this method takes ownership of |impl| depends on |ImplPtr|. If
  // |ImplPtr| is a raw pointer, then this method does not take ownership of
  // |impl|. If |ImplPtr| is a |unique_ptr|, then running |~ImplPtr| when the
  // binding generates an error will delete |impl| because |~ImplPtr| is
  // |~unique_ptr|, which deletes |impl|.
  //
  // The impl will use the given async_t (e.g., a message loop) in order to read
  // messages from the channel and to monitor the channel for
  // |ZX_CHANNEL_PEER_CLOSED|. It is not necessary to use the same async_t for
  // each binding added.
  void AddBinding(ImplPtr impl, InterfaceRequest<Interface> request,
                  async_dispatcher_t* dispatcher) {
    std::lock_guard<std::mutex> guard(lock_);
    bindings_.push_back(std::make_unique<Binding>(
        std::forward<ImplPtr>(impl), std::move(request), dispatcher));
    auto* binding = bindings_.back().get();
    // Set the connection error handler for the newly added Binding to be a
    // function that will erase it from the vector.
    binding->set_error_handler(
        [binding, this](zx_status_t status) { this->RemoveOnError(binding); });
  }

  // Adds a binding to the set for the given implementation.
  //
  // Creates a channel for the binding and returns the client endpoint of
  // the channel as an |InterfaceHandle|. If |AddBinding| fails to create the
  // underlying channel, the returned |InterfaceHandle| will return false from
  // |is_valid()|.
  //
  // The given |ImplPtr| is bound to the newly created channel. The binding is
  // removed (and the |~ImplPtr| called) when the created binding has an error
  // (e.g., if the remote endpoint of the channel sends an invalid message).
  //
  // Whether this method takes ownership of |impl| depends on |ImplPtr|. If
  // |ImplPtr| is a raw pointer, then this method does not take ownership of
  // |impl|. If |ImplPtr| is a |unique_ptr|, then running |~ImplPtr| when the
  // binding generates an error will delete |impl| because |~ImplPtr| is
  // |~unique_ptr|, which deletes |impl|.
  InterfaceHandle<Interface> AddBinding(ImplPtr impl,
                                        async_dispatcher_t* dispatcher) {
    InterfaceHandle<Interface> handle;
    InterfaceRequest<Interface> request = handle.NewRequest();
    if (!request)
      return nullptr;
    AddBinding(std::forward<ImplPtr>(impl), std::move(request), dispatcher);
    return handle;
  }

  // Removes all the bindings from the set.
  //
  // Closes all the channels associated with this |BindingSet|.
  void CloseAll() {
    std::lock_guard<std::mutex> guard(lock_);
    bindings_.clear();
  }

 private:
  // Called when a binding has an error to remove the binding from the set.
  void RemoveOnError(Binding* binding) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = std::find_if(bindings_.begin(), bindings_.end(),
                           [binding](const std::unique_ptr<Binding>& b) {
                             return b.get() == binding;
                           });
    ZX_DEBUG_ASSERT(it != bindings_.end());
    (*it)->set_error_handler(nullptr);
    bindings_.erase(it);
  }

  std::mutex lock_;
  StorageType bindings_ __TA_GUARDED(lock_);
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_THREAD_SAFE_BINDING_SET_H_
