// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_ANY_H_
#define LIB_FIDL_LLCPP_INTERNAL_ANY_H_

#include <zircon/assert.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace fidl {
namespace internal {

template <typename Interface, size_t kCapacity, size_t kAlignment, bool kMovable>
class AnyImpl {
 public:
  // Creates an invalid container that does not hold an object.
  AnyImpl() = default;
  ~AnyImpl() { reset(); }

  AnyImpl(const AnyImpl& other) = delete;
  AnyImpl& operator=(const AnyImpl& other) = delete;

  // Moving an invalid |Any| results in another invalid |Any|.
  // Moving a valid |Any| invokes the move constructor of the contained type,
  // and resets the moved-from object back to an invalid state.
  AnyImpl(AnyImpl&& other) noexcept {
    if constexpr (kMovable) {
      MoveImpl(std::move(other));
    }
  }
  AnyImpl& operator=(AnyImpl&& other) noexcept {
    if (this != &other) {
      reset();
      if constexpr (kMovable) {
        MoveImpl(std::move(other));
      }
    }
    return *this;
  }

  bool is_valid() const { return interface_ != nullptr; }

  // Initializes the container with an object of type |T|.
  //
  // Example:
  //
  //     Any<Animal> animal;
  //     animal.emplace<Dog>(args_to_dog_constructor);
  //
  template <typename T, typename... Args>
  T& emplace(Args&&... args) {
    static_assert(std::is_base_of<Interface, T>::value, "|T| must implement |Interface|");
    static_assert(sizeof(T) <= kCapacity,
                  "type does not fit inside storage, consider increase the storage limit");
    static_assert(alignof(T) <= kAlignment, "type has stricter alignment constraints than storage");

    reset();

    if constexpr (kMovable) {
      move_ = MoveConstructImpl<T>;
    }
    interface_ = AdjustPointerImpl<T>(storage_);
    return *new (&storage_) T(std::forward<Args>(args)...);
  }

  const Interface* operator->() const {
    ZX_DEBUG_ASSERT(is_valid());
    return interface_;
  }

  Interface* operator->() {
    ZX_DEBUG_ASSERT(is_valid());
    return interface_;
  }

  const Interface& operator*() const {
    ZX_DEBUG_ASSERT(is_valid());
    return *interface_;
  }

  Interface& operator*() {
    ZX_DEBUG_ASSERT(is_valid());
    return *interface_;
  }

 private:
  using Storage = std::aligned_storage_t<kCapacity, kAlignment>;

  void reset() {
    if (is_valid()) {
      interface_->~Interface();
    }
    interface_ = nullptr;
    if constexpr (kMovable) {
      move_ = nullptr;
    }
  }

  void MoveImpl(AnyImpl&& other) {
    ZX_DEBUG_ASSERT(kMovable);
    move_ = other.move_;
    if (move_) {
      move_(storage_, other.storage_, &interface_);
    }
    other.reset();
  }

  template <typename T>
  static Interface* AdjustPointerImpl(Storage& storage) {
    // NOTE: this may look like a no-op but is not the case. Consider a
    // situation where |T| is a class which uses multiple inheritance, and
    // |Interface| is not the first class in the inheritance chain. It is not a
    // guarantee that the pointer to the |Interface| facet of |T| is the same as
    // the pointer to |T|. A static cast from a |T*| to |Interface*| will take
    // this into account and give us the proper offset into the class hierarchy.
    return static_cast<Interface*>(reinterpret_cast<T*>(&storage));
  }

  template <typename T>
  static void MoveConstructImpl(Storage& dest, Storage& source, Interface** interface) {
    new (&dest) T(std::move(*reinterpret_cast<T*>(&source)));
    *interface = AdjustPointerImpl<T>(dest);
  }

  Storage storage_;

  Interface* interface_ = nullptr;

  // When no object is stored, |move_| is nullptr. When some object of type |T|
  // is stored, |move_| points to a function which moves an instance of |T|
  // stored in |source| into |dest|, then adjusts the |interface| pointer to
  // point to the correct offset within |dest|.
  void (*move_)(Storage& dest, Storage& source, Interface** interface) = nullptr;
};

// |Any| is a polymorphic container used to implement type erasure.
//
// Unlike |NonMovableAny|, |Any| is movable and any type placed inside of it
// must be movable.
//
// It is similar to |std::any|, with the following notable differences:
// * The contained object must implement (i.e. be a subclass of) |Interface|.
// * It will never heap allocate.
//
// This avoids additional memory allocations while using a virtual interface.
// |kCapacity| must be larger than the sizes of all of the individual |Interface| implementations.
template <typename Interface, size_t kCapacity = 16ull, size_t kAlignment = 16ull>
using Any = AnyImpl<Interface, kCapacity, kAlignment, true>;

// |NonMovableAny| is a polymorphic container used to implement type erasure.
//
// Unlike |Any|, |NonMovable| cannot be moved, but it can hold non-movable types.
//
// It is similar to |std::any|, with the following notable differences:
// * The contained object must implement (i.e. be a subclass of) |Interface|.
// * It will never heap allocate.
//
// This avoids additional memory allocations while using a virtual interface.
// |kCapacity| must be larger than the sizes of all of the individual |Interface| implementations.
template <typename Interface, size_t kCapacity = 16ull, size_t kAlignment = 16ull>
class NonMovableAny : public AnyImpl<Interface, kCapacity, kAlignment, false> {
 public:
  NonMovableAny() = default;

  NonMovableAny(NonMovableAny&&) = delete;
  NonMovableAny& operator=(NonMovableAny&&) = delete;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_INTERNAL_ANY_H_
