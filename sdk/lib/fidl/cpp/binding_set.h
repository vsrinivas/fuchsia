// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDING_SET_H_
#define LIB_FIDL_CPP_BINDING_SET_H_

#include <lib/fit/function.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

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
class BindingSet final {
 public:
  using Binding = ::fidl::Binding<Interface, ImplPtr>;
  using StorageType = std::vector<std::unique_ptr<Binding>>;
  // TODO(FIDL-761) Use fit::function here instead of std::function.
  using ErrorHandler = std::function<void(zx_status_t)>;

  using iterator = typename StorageType::iterator;

  BindingSet() = default;

  BindingSet(const BindingSet&) = delete;
  BindingSet& operator=(const BindingSet&) = delete;

  // The implementation of this class provides external references to class members via pointers.
  // As a result, instances cannot be move-constructed or move-assigned.
  BindingSet(BindingSet&&) = delete;
  BindingSet& operator=(BindingSet&&) = delete;

  // Adds a binding to the set.
  //
  // The given |ImplPtr| is bound to the channel underlying the
  // |InterfaceRequest|. The binding is removed (and the |~ImplPtr| called)
  // when the created binding has an error (e.g., if the remote endpoint of
  // the channel sends an invalid message). The binding can also be removed
  // by calling |RemoveBinding()|.
  //
  // Whether this method takes ownership of |impl| depends on |ImplPtr|. If
  // |ImplPtr| is a raw pointer, then this method does not take ownership of
  // |impl|. If |ImplPtr| is a |unique_ptr|, then running |~ImplPtr| when the
  // binding generates an error will delete |impl| because |~ImplPtr| is
  // |~unique_ptr|, which deletes |impl|.
  void AddBinding(ImplPtr impl, InterfaceRequest<Interface> request,
                  async_dispatcher_t* dispatcher = nullptr, ErrorHandler handler = nullptr) {
    bindings_.push_back(
        std::make_unique<Binding>(std::forward<ImplPtr>(impl), std::move(request), dispatcher));
    auto* binding = bindings_.back().get();
    // Set the connection error handler for the newly added Binding to be a
    // function that will erase it from the vector.
    binding->set_error_handler(
        [binding, capture_handler = std::move(handler), this](zx_status_t status) {
          // Subtle behavior: it is necessary to std::move into a local
          // variable because the closure is deleted when RemoveOnError
          // is called.
          ErrorHandler handler(std::move(capture_handler));
          this->RemoveOnError(binding);
          if (handler) {
            handler(status);
          }
        });
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
  InterfaceHandle<Interface> AddBinding(ImplPtr impl, async_dispatcher_t* dispatcher = nullptr,
                                        ErrorHandler handler = nullptr) {
    InterfaceHandle<Interface> handle;
    InterfaceRequest<Interface> request = handle.NewRequest();
    if (!request)
      return nullptr;
    AddBinding(std::forward<ImplPtr>(impl), std::move(request), dispatcher, std::move(handler));
    return handle;
  }

  // Removes a binding from the set.
  //
  // Returns true iff the binding was successfully found and removed.
  // Upon removal, the server endpoint of the channel is closed.
  template <class T>
  bool RemoveBinding(const T& impl) {
    return RemoveMatchedBinding([&impl](const std::unique_ptr<Binding>& b) {
      return ResolvePtr(impl) == ResolvePtr(b->impl());
    });
  }

  // Returns an InterfaceRequestHandler that binds the incoming
  // InterfaceRequests this object.
  InterfaceRequestHandler<Interface> GetHandler(ImplPtr impl,
                                                async_dispatcher_t* dispatcher = nullptr) {
    return [this, impl, dispatcher](InterfaceRequest<Interface> request) {
      AddBinding(impl, std::move(request), dispatcher);
    };
  }

  // Removes all the bindings from the set.
  //
  // Closes all the channels associated with this |BindingSet|.
  // Bindings are destroyed AFTER it is removed from the bindings set. An
  // example of when this is useful is if an error handler on a binding has
  // some behavior where it needs to read from the binding set; the set would
  // then properly reflect that the binding is not present in the set.
  void CloseAll() {
    auto bindings_local = std::move(bindings_);
    bindings_.clear();
  }

  // Removes all the bindings from the set using the provided epitaph.
  //
  // Closes all the channels associated with this |BindingSet| after sending
  // an epitaph. As with CloseAll(void) above, bindings are destroyed after they
  // are removed from the bindings set.
  void CloseAll(zx_status_t epitaph_value) {
    auto bindings_local = std::move(bindings_);
    bindings_.clear();
    for (const auto& binding : bindings_local) {
      binding->Close(epitaph_value);
    }
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
  // Resolve smart pointers with get methods (e.g. shared_ptr, unique_ptr, etc).
  template <class T, std::enable_if_t<!std::is_pointer<T>::value>* = nullptr>
  static void* ResolvePtr(T& p) {
    return reinterpret_cast<void*>(p.get());
  }

  // Resolve raw pointers.
  template <class T, std::enable_if_t<std::is_pointer<T>::value>* = nullptr>
  static void* ResolvePtr(T p) {
    return reinterpret_cast<void*>(p);
  }

  // Called when a binding has an error to remove the binding from the set.
  void RemoveOnError(Binding* binding) {
    bool found = RemoveMatchedBinding(
        [binding](const std::unique_ptr<Binding>& b) { return b.get() == binding; });
    ZX_DEBUG_ASSERT(found);
  }

  bool RemoveMatchedBinding(std::function<bool(const std::unique_ptr<Binding>&)> binding_matcher) {
    auto it = std::find_if(bindings_.begin(), bindings_.end(), binding_matcher);
    if (it == bindings_.end())
      return false;

    {
      // Move ownership of binding out of storage, such that the binding is
      // destroyed AFTER it is removed from the bindings.
      auto binding_local = std::move(*it);
      binding_local->set_error_handler(nullptr);
      bindings_.erase(it);
    }

    if (bindings_.empty() && empty_set_handler_)
      empty_set_handler_();
    return true;
  }

  StorageType bindings_;
  fit::closure empty_set_handler_;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDING_SET_H_
