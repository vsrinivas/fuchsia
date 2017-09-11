// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_STRUCT_PTR_H_
#define LIB_FIDL_CPP_BINDINGS_STRUCT_PTR_H_

#include <cstddef>
#include <memory>
#include <new>

#include "lib/fidl/cpp/bindings/macros.h"
#include "lib/fidl/cpp/bindings/type_converter.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace fidl {
namespace internal {

template <typename Struct>
class StructHelper {
 public:
  template <typename Ptr>
  static void Initialize(Ptr* ptr) {
    ptr->Initialize();
  }
};

}  // namespace internal

// Smart pointer wrapping a mojom structure or union, with move-only semantics.
template <typename Struct>
class StructPtr {
 public:
  StructPtr() {}
  StructPtr(std::nullptr_t) {}

  ~StructPtr() {}

  StructPtr& operator=(std::nullptr_t) {
    reset();
    return *this;
  }

  StructPtr(StructPtr&& other) { Take(&other); }
  StructPtr& operator=(StructPtr&& other) {
    Take(&other);
    return *this;
  }

  template <typename U>
  U To() const {
    return TypeConverter<U, StructPtr>::Convert(*this);
  }

  void reset() { ptr_.reset(); }

  // Tests as true if non-null, false if null.
  explicit operator bool() const { return !!ptr_; }

  bool is_null() const { return !ptr_; }

  Struct& operator*() const {
    FXL_DCHECK(ptr_);
    return *ptr_;
  }
  Struct* operator->() const {
    FXL_DCHECK(ptr_);
    return ptr_.get();
  }
  Struct* get() const { return ptr_.get(); }

  void Swap(StructPtr* other) { std::swap(ptr_, other->ptr_); }

  // Please note that calling this method will fail compilation if the value
  // type |Struct| doesn't have a Clone() method defined (which usually means
  // that it contains Mojo handles).
  StructPtr Clone() const { return is_null() ? StructPtr() : ptr_->Clone(); }

  bool Equals(const StructPtr& other) const {
    if (is_null() || other.is_null())
      return is_null() && other.is_null();
    return ptr_->Equals(*other.ptr_);
  }

 private:
  friend class internal::StructHelper<Struct>;
  void Initialize() {
    FXL_DCHECK(!ptr_);
    ptr_.reset(new Struct());
  }

  void Take(StructPtr* other) {
    reset();
    Swap(other);
  }

  std::unique_ptr<Struct> ptr_;

  FIDL_MOVE_ONLY_TYPE(StructPtr);
};

// Designed to be used when Struct is small and copyable. Unions are always
// InlinedStructPtr in practice.
template <typename Struct>
class InlinedStructPtr {
 public:
  InlinedStructPtr() : is_null_(true) {}
  InlinedStructPtr(std::nullptr_t) : is_null_(true) {}

  ~InlinedStructPtr() {}

  InlinedStructPtr& operator=(std::nullptr_t) {
    reset();
    return *this;
  }

  InlinedStructPtr(InlinedStructPtr&& other) : is_null_(true) { Take(&other); }
  InlinedStructPtr& operator=(InlinedStructPtr&& other) {
    Take(&other);
    return *this;
  }

  template <typename U>
  U To() const {
    return TypeConverter<U, InlinedStructPtr>::Convert(*this);
  }

  void reset() {
    is_null_ = true;
    value_.~Struct();
    new (&value_) Struct();
  }

  // Tests as true if non-null, false if null.
  explicit operator bool() const { return !is_null_; }

  bool is_null() const { return is_null_; }

  Struct& operator*() const {
    FXL_DCHECK(!is_null_);
    return value_;
  }
  Struct* operator->() const {
    FXL_DCHECK(!is_null_);
    return &value_;
  }
  Struct* get() const { return is_null() ? nullptr : &value_; }

  void Swap(InlinedStructPtr* other) {
    std::swap(value_, other->value_);
    std::swap(is_null_, other->is_null_);
  }

  InlinedStructPtr Clone() const {
    return is_null() ? InlinedStructPtr() : value_.Clone();
  }
  bool Equals(const InlinedStructPtr& other) const {
    if (is_null() || other.is_null())
      return is_null() && other.is_null();
    return value_.Equals(other.value_);
  }

 private:
  friend class internal::StructHelper<Struct>;
  void Initialize() { is_null_ = false; }

  void Take(InlinedStructPtr* other) {
    reset();
    Swap(other);
  }

  mutable Struct value_;
  bool is_null_;

  FIDL_MOVE_ONLY_TYPE(InlinedStructPtr);
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_STRUCT_PTR_H_
