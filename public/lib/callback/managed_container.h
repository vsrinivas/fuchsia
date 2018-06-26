// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CALLBACK_MANAGED_CONTAINER_H_
#define LIB_CALLBACK_MANAGED_CONTAINER_H_

#include <memory>
#include <utility>
#include <vector>

#include <lib/fit/function.h>

#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace callback {

// ManagedContainer is a heterogeneous container that allows to share ownership
// of any added element between the container and the returned managed element.
// The element will be deleted as soon as either the container or the returned
// managed element is deleted.
//
// Example usage:
//
//   ManagedContainer container;
//
//   E element;
//   auto managed_element = container.Manage(std::move(element));
//
// From here, |element| can be accessed through |managed_element| and will be
// deleted when either |container| or |managed_element| is deleted. In
// particular, |*managed_element| must not be accessed after |container| is
// deleted, but |managed_element| itself can outlive |container|.
class ManagedContainer {
 public:
  ManagedContainer();
  ~ManagedContainer();

  template <typename E>
  class ManagedElement {
   public:
    ManagedElement(E* element, fit::closure cleanup)
        : element_(element), auto_cleanup_(std::move(cleanup)) {}
    ManagedElement(ManagedElement<E>&& other) noexcept = default;
    ManagedElement& operator=(ManagedElement<E>&& other) noexcept = default;

    E* get() const { return element_; }

    E& operator*() const { return *element_; }

    E* operator->() const { return element_; }

    void reset() { auto_cleanup_.call(); }

   private:
    E* element_;
    fxl::AutoCall<fit::closure> auto_cleanup_;
  };

  // Manage() takes an object |element| as input, and returns a ManagedElement.
  // The |ManagedElement| acts as a pointer to |element|. |element| will be
  // deleted when either the returned object or the container itself is deleted.
  // The returned ManagedElement must not be dereferenced after the container
  // has been deleted.
  template <typename E>
  ManagedElement<E> Manage(E element) {
    auto typed_element = std::make_unique<TypedElement<E>>(std::move(element));
    E* result = typed_element->element();
    fit::closure cleanup = ManageElement(std::move(typed_element));
    return ManagedElement<E>(result, std::move(cleanup));
  }

  size_t empty() { return managed_elements_.empty(); }

 private:
  class Element {
   public:
    Element() {}
    virtual ~Element() {}

   private:
    FXL_DISALLOW_COPY_AND_ASSIGN(Element);
  };

  template <typename A>
  class TypedElement : public Element {
   public:
    explicit TypedElement(A element) : element_(std::move(element)) {}

    A* element() { return &element_; }

   private:
    A element_;
  };

  fit::closure ManageElement(std::unique_ptr<Element> element);

  std::vector<std::unique_ptr<Element>> managed_elements_;
  fxl::WeakPtrFactory<ManagedContainer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ManagedContainer);
};

}  // namespace callback

#endif  // LIB_CALLBACK_MANAGED_CONTAINER_H_
