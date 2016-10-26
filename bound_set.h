// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>

#include <vector>

#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"

namespace maxwell {

template <typename T>
T Identity(T t) {
  return t;
}

template <typename T>
T* Unwrap(std::unique_ptr<T>* p) {
  return p->get();
}

// General implementation intended to cover Binding and StrongBinding.
template <typename MojoType, typename UniqueType = MojoType*>
UniqueType Identify(MojoType* binding) {
  return binding;
}

// "Specialization" (overload) covering InterfacePtr.
template <typename Interface, typename UniqueType = Interface*>
UniqueType Identify(mojo::InterfacePtr<Interface>* ip) {
  return ip->get();
}

// An extensible/derivable InterfacePtrSet/(Strong)BindingSet that contains a
// collection of objects of type T that contain MojoTypes (e.g. InterfacePtr,
// Binding, or StrongBinding). Elements are automatically removed from the
// collection and destroyed when their associated MessagePipe experiences a
// connection error. When the set is destroyed all of the MessagePipes will be
// closed.
//
// Unlike the Mojo library InterfacePtrSet and (Strong)BindingSet, this class
// does not prevent further mutations to the underlying Mojo type. For well-
// defined behavior, the Mojo types should not be modified after being added to
// the set.
//
// Template parameters:
// * MojoType - the Mojo type governing each element of the collection, e.g.
//  InterfacePtr, Binding, or StrongBinding
// * T - the element type of the collection
// * GetMojoType - a function that extracts the MojoType from an element of the
//  collection. Defaults to the identity function, which only works if T =
//  MojoType.
// * UniqueType - a type that uniquely identifies a MojoType in the collection.
//  For InterfacePtrs, this is a pointer to the interface. For bindings, this is
//  the pointer to the binding.
// * Identify - a function that derives a UniqueType from a MojoType. Defaults
//  behave as described at UniqueType.
template <typename MojoType,
          typename T = MojoType,
          MojoType* GetMojoType(T*) = Identity,
          typename UniqueType = void*,
          UniqueType Identify(MojoType*) = Identify>
class BoundSet {
 public:
  typedef typename std::vector<T>::iterator iterator;
  BoundSet() {}
  virtual ~BoundSet() {}

  // |ptr| must be bound to a message pipe.
  template <typename... _Args>
  T* emplace(_Args&&... __args) {
    elements_.emplace_back(std::forward<_Args>(__args)...);
    T* const c = &elements_.back();
    MojoType* const m = GetMojoType(c);
    assert(m->is_bound());
    UniqueType const id = Identify(m);
    // Set the connection error handler for the newly added item to be a
    // function that will erase it from the vector.
    m->set_connection_error_handler([this, id] { OnConnectionError(id); });
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
  virtual void OnConnectionError(UniqueType id) {
    auto it = Find(id);
    assert(it != elements_.end());
    elements_.erase(it);
  }

 private:
  iterator Find(UniqueType id) {
    return std::find_if(elements_.begin(), elements_.end(), [this, id](T& e) {
      return Identify(GetMojoType(&e)) == id;
    });
  }

  std::vector<T> elements_;
};

// Convenience alias of BoundSet to handle InterfacePtr containers.
//
// Since InterfacePtr itself is a movable type, the thing that uniquely
// identifies the InterfacePtr we wish to erase is its Interface*.
template <typename Interface,
          typename T = mojo::InterfacePtr<Interface>,
          mojo::InterfacePtr<Interface>* GetMojoType(T*) = Identity>
using BoundPtrSet =
    BoundSet<mojo::InterfacePtr<Interface>, T, GetMojoType, Interface*>;

// Convenience alias of BoundSet to handle Binding containers.
//
// Note that the default T here must be a unique_ptr rather than the Binding
// itself since Bindings are not movable.
template <typename Interface,
          typename T = std::unique_ptr<mojo::Binding<Interface>>,
          mojo::Binding<Interface>* GetMojoType(T*) = Unwrap>
using BindingSet = BoundSet<mojo::Binding<Interface>,
                            T,
                            GetMojoType,
                            mojo::Binding<Interface>*>;

}  // namespace maxwell
