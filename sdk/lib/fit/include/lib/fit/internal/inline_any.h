// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_INCLUDE_LIB_FIT_INTERNAL_INLINE_ANY_H_
#define LIB_FIT_INCLUDE_LIB_FIT_INTERNAL_INLINE_ANY_H_

#include <lib/stdcompat/type_traits.h>
#include <lib/stdcompat/utility.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <type_traits>
#include <utility>

#include "../traits.h"
#include "utility.h"

namespace fit {
namespace internal {

enum class inline_any_is_pinned : bool { no = false, yes = true };

template <typename I, size_t Reserve, size_t Align, inline_any_is_pinned Pinned>
class inline_any_impl {
  static constexpr size_t storage_size = std::max(sizeof(I), Reserve);
  static constexpr size_t storage_alignment = std::max(alignof(I), Align);

  using storage = std::aligned_storage_t<storage_size, storage_alignment>;

  template <typename... Conditions>
  using requires_conditions = ::fit::internal::requires_conditions<Conditions...>;

  template <typename... Conditions>
  using assignment_requires =
      ::fit::internal::assignment_requires_conditions<inline_any_impl&, Conditions...>;

  template <typename T>
  using not_self_type = ::fit::internal::not_same_type<inline_any_impl, T>;

  template <typename T>
  using fits = cpp17::bool_constant<sizeof(cpp20::remove_cvref_t<T>) <= storage_size &&
                                    alignof(cpp20::remove_cvref_t<T>) <= storage_alignment>;

  template <typename T>
  using is_compatible =
      cpp17::conjunction<std::is_base_of<I, cpp20::remove_cvref_t<T>>, not_self_type<T>, fits<T>>;

 public:
  // Constructs an empty container.
  inline_any_impl() = default;

  ~inline_any_impl() { reset(); }

  template <typename T, requires_conditions<is_compatible<T>> = 0>
  explicit inline_any_impl(T&& value) : op_{get_op_fn<T>()} {
    new (&storage_) cpp20::remove_cvref_t<T>(std::forward<T>(value));
  }

  template <typename T, typename... Args,
            requires_conditions<std::is_constructible<T, Args...>, is_compatible<T>> = 0>
  explicit inline_any_impl(cpp17::in_place_type_t<T>, Args&&... args) : op_{get_op_fn<T>()} {
    new (&storage_) cpp20::remove_cvref_t<T>(std::forward<Args>(args)...);
  }

  inline_any_impl(const inline_any_impl& other) : op_{other.op_} {
    op_(opcode::copy_construct, &storage_, const_cast<storage*>(&other.storage_));
  }
  inline_any_impl(inline_any_impl&& other) noexcept : op_{other.op_} {
    op_(opcode::move_construct, &storage_, &other.storage_);
    other.reset();
  }

  template <typename T>
  // |assignment_requires| becomes |inline_any_impl&| when the condition is satisfied.
  // NOLINTNEXTLINE: clang-tidy doesn't understand |assignment_requires|.
  assignment_requires<is_compatible<T>> operator=(T&& value) {
    if (op_ == get_op_fn<T>()) {
      reference<T>(&storage_) = std::forward<T>(value);
    } else {
      op_(opcode::destroy, &storage_, nullptr);
      op_ = get_op_fn<T>();
      new (&storage_) cpp20::remove_cvref_t<T>(std::forward<T>(value));
    }
    return *this;
  }

  inline_any_impl& operator=(const inline_any_impl& other) {
    if (this != &other) {
      if (op_ == other.op_) {
        op_(opcode::copy, &storage_, const_cast<storage*>(&other.storage_));
      } else {
        op_(opcode::destroy, &storage_, nullptr);
        op_ = other.op_;
        op_(opcode::copy_construct, &storage_, const_cast<storage*>(&other.storage_));
      }
    }
    return *this;
  }
  inline_any_impl& operator=(inline_any_impl&& other) noexcept {
    if (this != &other) {
      if (op_ == other.op_) {
        op_(opcode::move, &storage_, &other.storage_);
      } else {
        op_(opcode::destroy, &storage_, nullptr);
        op_ = other.op_;
        op_(opcode::move_construct, &storage_, &other.storage_);
      }
      other.reset();
    }
    return *this;
  }

  // Initializes the container with an object of type |T|.
  //
  // Example:
  //
  //     fit::inline_any<Animal> animal;
  //     animal.emplace<Dog>(args_to_dog_constructor);
  //
  template <typename T, typename... Args, requires_conditions<is_compatible<T>> = 0>
  T& emplace(Args&&... args) {
    reset();
    op_ = get_op_fn<T>();
    new (&storage_) cpp20::remove_cvref_t<T>(std::forward<Args>(args)...);
    return as<T>();
  }

  // Resets the container back to an empty state.
  void reset() {
    op_(opcode::destroy, &storage_, nullptr);
    op_ = default_op_fn;
  }

  // Whether the container contains an object.
  bool has_value() const { return op_ != default_op_fn; }

  // Whether the stored object type is |T|.
  template <typename T, requires_conditions<is_compatible<T>> = 0>
  bool is() const {
    return op_ == get_op_fn<T>();
  }

  I* operator->() {
    if (op_ != default_op_fn) {
      return op_(opcode::get_interface, &storage_, nullptr);
    }
    __builtin_abort();
  }
  const I* operator->() const {
    if (op_ != default_op_fn) {
      return op_(opcode::get_interface, const_cast<storage*>(&storage_), nullptr);
    }
    __builtin_abort();
  }

  // Asserts that the stored object type is |T|, then access it.
  template <typename T, requires_conditions<is_compatible<T>> = 0>
  T& as() {
    if (op_ == get_op_fn<T>()) {
      return reference<T>(&storage_);
    }
    __builtin_abort();
  }
  template <typename T, requires_conditions<is_compatible<T>> = 0>
  const T& as() const {
    if (op_ == get_op_fn<T>()) {
      return const_reference<T>(&storage_);
    }
    __builtin_abort();
  }

  // Asserts that an object is contained, then invokes |visitor| with a reference.
  template <typename Callable, requires_conditions<cpp17::is_invocable<Callable, I&>> = 0>
  decltype(auto) visit(Callable&& visitor) {
    if (op_ != default_op_fn) {
      return std::forward<Callable>(visitor)(*op_(opcode::get_interface, &storage_, nullptr));
    }
    __builtin_abort();
  }
  template <typename Callable, requires_conditions<cpp17::is_invocable<Callable, const I*>> = 0>
  decltype(auto) visit(Callable&& visitor) const {
    if (op_ != default_op_fn) {
      return std::forward<Callable>(visitor)(op_(opcode::get_interface, &storage_, nullptr));
    }
    __builtin_abort();
  }

  // Asserts that an object is contained, then invokes |visitor| with a pointer.
  template <typename Callable, requires_conditions<cpp17::is_invocable<Callable, I*>> = 0>
  decltype(auto) visit(Callable&& visitor) {
    if (op_ != default_op_fn) {
      return std::forward<Callable>(visitor)(op_(opcode::get_interface, &storage_, nullptr));
    }
    __builtin_abort();
  }
  template <typename Callable, requires_conditions<cpp17::is_invocable<Callable, const I&>> = 0>
  decltype(auto) visit(Callable&& visitor) const {
    if (op_ != default_op_fn) {
      return std::forward<Callable>(visitor)(*op_(opcode::get_interface, &storage_, nullptr));
    }
    __builtin_abort();
  }

  // Asserts that the stored object type is |T|, then invokes |visitor| with a reference.
  template <typename T, typename Callable,
            requires_conditions<is_compatible<T>, cpp17::is_invocable<Callable, T&>> = 0>
  decltype(auto) visit_as(Callable&& visitor) {
    if (op_ == get_op_fn<T>()) {
      return std::forward<Callable>(visitor)(reference<T>(&storage_));
    }
    __builtin_abort();
  }
  template <typename T, typename Callable,
            requires_conditions<is_compatible<T>, cpp17::is_invocable<Callable, const T&>> = 0>
  decltype(auto) visit_as(Callable&& visitor) const {
    if (op_ == get_op_fn<T>()) {
      return std::forward<Callable>(visitor)(const_reference<T>(&storage_));
    }
    __builtin_abort();
  }

  // Asserts that the stored object type is |T|, then invokes |visitor| with a pointer.
  template <typename T, typename Callable,
            requires_conditions<is_compatible<T>, cpp17::is_invocable<Callable, T*>> = 0>
  decltype(auto) visit_as(Callable&& visitor) {
    if (op_ == get_op_fn<T>()) {
      return std::forward<Callable>(visitor)(pointer<T>(&storage_));
    }
    __builtin_abort();
  }
  template <typename T, typename Callable,
            requires_conditions<is_compatible<T>, cpp17::is_invocable<Callable, const T*>> = 0>
  decltype(auto) visit_as(Callable&& visitor) const {
    if (op_ == get_op_fn<T>()) {
      return std::forward<Callable>(visitor)(const_pointer<T>(&storage_));
    }
    __builtin_abort();
  }

 private:
  enum class opcode {
    get_interface,
    destroy,

    // The following are only used when |!Pinned|.
    copy_construct,
    copy,
    move_construct,
    move,
  };

  using op_fn = I* (*)(opcode op, storage* self, storage* other);

  static I* default_op_fn(opcode, storage*, storage*) { return nullptr; }

  template <typename T>
  static cpp20::remove_cvref_t<T>* pointer(storage* storage) {
    return reinterpret_cast<cpp20::remove_cvref_t<T>*>(storage);
  }
  template <typename T>
  static const cpp20::remove_cvref_t<T>* const_pointer(const storage* storage) {
    return reinterpret_cast<const cpp20::remove_cvref_t<T>*>(storage);
  }
  template <typename T>
  static cpp20::remove_cvref_t<T>& reference(storage* storage) {
    return *pointer<T>(storage);
  }
  template <typename T>
  static const cpp20::remove_cvref_t<T>& const_reference(const storage* storage) {
    return *const_pointer<T>(storage);
  }

  template <typename T, inline_any_is_pinned pinned>
  struct type_op_fn;

  // Pinned.
  template <typename T>
  struct type_op_fn<T, inline_any_is_pinned::yes> {
    template <typename U = T>
    static I* fn(opcode op, storage* self, storage* other) {
      switch (op) {
        case opcode::get_interface:
          return static_cast<I*>(pointer<T>(self));
        case opcode::destroy:
          pointer<T>(self)->T::~T();
          return nullptr;
        default:
          __builtin_abort();
      }
    }
  };

  // Not pinned.
  template <typename T>
  struct type_op_fn<T, inline_any_is_pinned::no> {
    template <typename U = T,
              requires_conditions<std::is_move_constructible<T>, std::is_copy_constructible<T>> = 0>
    static I* fn(opcode op, storage* self, storage* other) {
      switch (op) {
        case opcode::get_interface:
          return static_cast<I*>(pointer<T>(self));
        case opcode::destroy:
          pointer<T>(self)->T::~T();
          return nullptr;
        case opcode::copy_construct:
          new (self) T(const_reference<T>(other));
          return nullptr;
        case opcode::copy:
          reference<T>(self) = const_reference<T>(other);
          return nullptr;
        case opcode::move_construct:
          new (self) T(std::move(reference<T>(other)));
          return nullptr;
        case opcode::move:
          reference<T>(self) = std::move(reference<T>(other));
          return nullptr;
        default:
          __builtin_abort();
      }
    }
  };

  template <typename T>
  static op_fn get_op_fn() {
    return type_op_fn<cpp20::remove_cvref_t<T>, Pinned>::fn;
  }

  storage storage_;
  op_fn op_{default_op_fn};
};

}  // namespace internal
}  // namespace fit

#endif  // LIB_FIT_INCLUDE_LIB_FIT_INTERNAL_INLINE_ANY_H_
