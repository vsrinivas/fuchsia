// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>

#include <vector>

#include "mojo/public/cpp/bindings/interface_ptr.h"

namespace maxwell {

// An extensible/derivable InterfacePtrSet that contains a collection of objects
// of type T that contain InterfacePtr<I>s. Elements are automatically removed
// from the collection and destroyed when their associated MessagePipe
// experiences a connection error. When the set is destroyed all of the
// MessagePipes will be closed.
template <typename T, typename I>
class BoundSet {
 public:
  typedef typename std::vector<T>::iterator iterator;
  BoundSet() {}
  virtual ~BoundSet() {}

  // |ptr| must be bound to a message pipe.
  template <typename... _Args>
  void emplace(_Args&&... __args) {
    elements_.emplace_back(std::forward<_Args>(__args)...);
    mojo::InterfacePtr<I>* ipp = GetPtr(&elements_.back());
    assert(ipp->is_bound());
    I* pointer = ipp->get();
    // Set the connection error handler for the newly added InterfacePtr to be a
    // function that will erase it from the vector.
    ipp->set_connection_error_handler(
        [pointer, this] { OnConnectionError(pointer); });
  }

  // Removes the element at the given iterator. This effectively closes the pipe
  // there if open, but it does not call OnConnectionError.
  iterator erase(iterator it) { return elements_.erase(it); }

  // Closes the MessagePipe associated with each of the InterfacePtrs in
  // this set and clears the set. This does not call OnConnectionError for every
  // interface in the set.
  void clear() { elements_.clear(); }
  bool empty() const { return elements_.empty(); }
  size_t size() const { return elements_.size(); }

  iterator begin() { return elements_.begin(); }
  iterator end() { return elements_.end(); }

 protected:
  virtual mojo::InterfacePtr<I>* GetPtr(T* element) = 0;

  // Since InterfacePtr itself is a movable type, the thing that uniquely
  // identifies the InterfacePtr we wish to erase is its Interface*.
  virtual void OnConnectionError(I* interface_ptr) {
    auto it = Find(interface_ptr);
    assert(it != elements_.end());
    elements_.erase(it);
  }

 private:
  iterator Find(I* interface_ptr) {
    return std::find_if(elements_.begin(), elements_.end(),
                        [this, interface_ptr](T& e) {
                          return GetPtr(&e)->get() == interface_ptr;
                        });
  }

  std::vector<T> elements_;
};

template <typename I>
class ExtensibleInterfacePtrSet : public BoundSet<mojo::InterfacePtr<I>, I> {
 protected:
  mojo::InterfacePtr<I>* GetPtr(mojo::InterfacePtr<I>* element) override {
    return element;
  }
};

}  // namespace mojo
