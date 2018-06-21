// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDING_SET_H_
#define LIB_FIDL_CPP_BINDING_SET_H_

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <lib/fit/function.h>

#include "lib/fidl/cpp/binding.h"

namespace fidl {

// Manages a set of bindings to implementations owned by the bound channels.
//
// The implementation pointer type of the binding is also parameterized,
// allowing the use of smart pointer types such as |std::unique_ptr<>| to
// reference the implementation.
//
// See also:
//
//  * |InterfacePtrSet|, which is the client analog of |BindingSet|.
template <typename Interface, typename ImplPtr = Interface*>
class BindingSet {
 public:
  using Binding = ::fidl::Binding<Interface, ImplPtr>;
  using StorageType = std::vector<std::unique_ptr<Binding>>;

  using iterator = typename StorageType::iterator;
  using const_iterator = typename StorageType::const_iterator;

  BindingSet() = default;

  BindingSet(const BindingSet&) = delete;
  BindingSet& operator=(const BindingSet&) = delete;

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
  void AddBinding(ImplPtr impl, InterfaceRequest<Interface> request) {
    bindings_.push_back(std::make_unique<Binding>(std::forward<ImplPtr>(impl),
                                                  std::move(request)));
    auto* binding = bindings_.back().get();
    // Set the connection error handler for the newly added Binding to be a
    // function that will erase it from the vector.
    binding->set_error_handler(
        std::bind(&BindingSet::RemoveOnError, this, binding));
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
  InterfaceHandle<Interface> AddBinding(ImplPtr impl) {
    InterfaceHandle<Interface> handle;
    InterfaceRequest<Interface> request = handle.NewRequest();
    if (!request)
      return nullptr;
    AddBinding(std::forward<ImplPtr>(impl), std::move(request));
    return handle;
  }

  // Returns an InterfaceRequestHandler that binds the incoming
  // InterfaceRequests this object.
  InterfaceRequestHandler<Interface> GetHandler(ImplPtr impl) {
    return [this, impl](InterfaceRequest<Interface> request) {
      AddBinding(impl, std::move(request));
    };
  }

  // Removes all the bindings from the set.
  //
  // Closes all the channels associated with this |BindingSet|.
  void CloseAll() { bindings_.clear(); }

  // Close a binding at the given iterator. The iterator must be valid and
  // dereferenceable.
  void CloseAndCheckForEmpty(const_iterator iterator) {
    (*iterator)->set_error_handler(nullptr);
    bindings_.erase(iterator);
    if (bindings_.empty() && empty_set_handler_)
      empty_set_handler_();
  }

  // The number of bindings in this |BindingSet|.
  size_t size() const { return bindings_.size(); }

  // Called when the last binding has been removed from this |BindingSet|.
  //
  // This function is not called by |CloseAll| or by |~BindingSet|.
  void set_empty_set_handler(fit::closure empty_set_handler) {
    empty_set_handler_ = std::move(empty_set_handler);
  }

  // The bindings stored in this set.
  //
  // This collection of bindings can be invalidated when a |Binding| in the
  // set encounters a connection error because connection errors causes the
  // |BindingSet| to remove the |Binding| from the set.
  const StorageType& bindings() const { return bindings_; }

 private:
  // Called when a binding has an error to remove the binding from the set.
  void RemoveOnError(Binding* binding) {
    auto it = std::find_if(bindings_.begin(), bindings_.end(),
                           [binding](const std::unique_ptr<Binding>& b) {
                             return b.get() == binding;
                           });
    ZX_DEBUG_ASSERT(it != bindings_.end());
    CloseAndCheckForEmpty(it);
  }

  StorageType bindings_;
  fit::closure empty_set_handler_;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDING_SET_H_
