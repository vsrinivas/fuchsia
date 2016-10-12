// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>

#include <vector>

#include "mojo/public/cpp/bindings/interface_ptr.h"

namespace maxwell {

template <typename T>
T Identity(T t) {
  return t;
}

// General implementation intended to cover Binding and StrongBinding.
template <typename Interface, typename MojoWrapper>
Interface* GetPtr(MojoWrapper* binding) {
  return binding->impl();
}

// "Specialization" (overload) covering InterfacePtr.
template <typename Interface>
Interface* GetPtr(mojo::InterfacePtr<Interface>* ip) {
  return ip->get();
}

// An extensible/derivable InterfacePtrSet/(Strong)BindingSet that contains a
// collection of objects of type T that contain MojoWrappers (e.g.
// InterfacePtr, Binding, or StrongBinding) of Interfaces. Elements are
// automatically removed from the collection and destroyed when their associated
// MessagePipe experiences a connection error. When the set is destroyed all of
// the MessagePipes will be closed.
template <typename Interface,
          typename MojoWrapper = mojo::InterfacePtr<Interface>,
          typename T = MojoWrapper,
          MojoWrapper* GetWrapper(T* element) = Identity,
          Interface* GetPtr(MojoWrapper* wrapper) = GetPtr>
class BoundSet {
 public:
  typedef typename std::vector<T>::iterator iterator;
  BoundSet() {}
  virtual ~BoundSet() {}

  // |ptr| must be bound to a message pipe.
  template <typename... _Args>
  T* emplace(_Args&&... __args) {
    elements_.emplace_back(std::forward<_Args>(__args)...);
    T* c = &elements_.back();
    MojoWrapper* w = GetWrapper(c);
    assert(w->is_bound());
    Interface* ptr = GetPtr(w);
    // Set the connection error handler for the newly added InterfacePtr to be a
    // function that will erase it from the vector.
    w->set_connection_error_handler([ptr, this] { OnConnectionError(ptr); });
    return c;
  }

  // Removes the element at the given iterator. This effectively closes the pipe
  // there if open, but it does not call OnConnectionError.
  iterator erase(iterator it) { return elements_.erase(it); }

  // Closes the MessagePipe associated with each of the items in this set and
  // clears the set. This does not call OnConnectionError for every interface in
  // the set.
  void clear() { elements_.clear(); }
  bool empty() const { return elements_.empty(); }
  size_t size() const { return elements_.size(); }

  iterator begin() { return elements_.begin(); }
  iterator end() { return elements_.end(); }

 protected:
  // Since InterfacePtr itself is a movable type, the thing that uniquely
  // identifies the InterfacePtr we wish to erase is its Interface*.
  virtual void OnConnectionError(Interface* interface_ptr) {
    auto it = Find(interface_ptr);
    assert(it != elements_.end());
    elements_.erase(it);
  }

 private:
  iterator Find(Interface* interface_ptr) {
    return std::find_if(elements_.begin(), elements_.end(),
                        [this, interface_ptr](T& e) {
                          return GetPtr(GetWrapper(&e)) == interface_ptr;
                        });
  }

  std::vector<T> elements_;
};

// Convenience alias of BoundSet to handle InterfacePtr containers. The default
// template parameter ordering places MojoWrapper before T and, necessarily,
// GetWrapper. This alias is a shorter way to leverage the covariant default of
// Mojowrapper to InterfacePtr<Interface>, and bakes in the GetPtr defaulting.
template <typename Interface,
          typename T = mojo::InterfacePtr<Interface>,
          mojo::InterfacePtr<Interface>* GetWrapper(T* element) = Identity>
using BoundPtrSet =
    BoundSet<Interface, mojo::InterfacePtr<Interface>, T, GetWrapper>;

}  // namespace maxwell
