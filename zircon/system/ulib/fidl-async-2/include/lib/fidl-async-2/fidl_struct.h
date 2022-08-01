// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_ASYNC_2_FIDL_STRUCT_H_
#define LIB_FIDL_ASYNC_2_FIDL_STRUCT_H_

#include <lib/fidl/cpp/wire/traits.h>
#include <lib/fidl/cpp/wire_natural_conversions.h>
#include <zircon/assert.h>

#include <type_traits>
#include <utility>

// TODO(dustingreen): Switch all client code to use llcpp directly instead of using this template.
//
// FidlCStruct is the FIDL C struct of a FIDL struct with [ForDeprecatedCBindings] - no OOB
// pointers; all in-band.  Support for OOB pointers is not anticipated.
//
// FidlLlcppStruct is the corresponding FIDL LLCPP struct.
//
// New uses of FidlStruct should be limited to temporary usages which will go away after everything
// is done moving from FIDL C to LLCPP, or from FIDL C to HLCPP.
template <typename FidlCStruct, typename FidlLlcppStruct>
class FidlStruct {
  // Sanity check to try to catch passing arguments which don't correspond.
  static_assert(sizeof(FidlCStruct) == sizeof(FidlLlcppStruct),
                "parameters must be for same struct");
  // FidlStruct<> isn't meant for use with FIDL types where HasPointer is true.
  static_assert(!fidl::TypeTraits<FidlLlcppStruct>::kHasPointer);
  static constexpr bool ProvideCopyAsLlcpp_v =
      (fidl::TypeTraits<FidlLlcppStruct>::kMaxNumHandles == 0);

 public:
  using c_type = FidlCStruct;
  using llcpp_type = FidlLlcppStruct;

  // These are used to select which constructor.
  enum DefaultType { Default };
  enum NullType { Null };

  // For request structs, the request handler is expected to close all the
  // handles, but the incoming struct itself isn't owned by the handler, and
  // the incoming struct is const, which conflicts with managing handles by
  // zeroing a handle field when the handle is closed.  So for now, we always
  // copy the incoming struct, own the copy, and close the handles via the
  // copy.  The _dispatch() caller won't try to close the handles in its copy.

  explicit FidlStruct(const FidlCStruct& to_copy_and_own_handles)
      // struct copy
      : storage_(to_copy_and_own_handles), ptr_(&storage_) {
    // nothing else to do here
  }

  explicit FidlStruct(FidlLlcppStruct&& to_move_and_own_handles) {
    ZX_DEBUG_ASSERT(!fidl::TypeTraits<FidlLlcppStruct>::kHasPointer);
    *reinterpret_cast<FidlLlcppStruct*>(&storage_) = std::move(to_move_and_own_handles);
    ptr_ = &storage_;
  }

  // There is intentionally not a zero-arg constructor, to force selection
  // between starting with default-initialized storage with handles owned by
  // ptr_ (any handles set to non-zero value after construction), vs. starting
  // with ptr_ set to nullptr so a later reset() is faster.

  // For reply structs, it's useful to start with a default-initialized struct
  // that can get incrementally populated, with a partially-initialized struct
  // along the way that's still possible to clean up so handles get closed
  // properly even if the reply never gets fully built and/or never gets sent.
  explicit FidlStruct(DefaultType not_used) : ptr_(&storage_) {
    // nothing else to do here
  }

  explicit FidlStruct(NullType not_used) : ptr_(nullptr) {
    // nothing else to do here
  }

  // Close any handles that aren't currently ZX_HANDLE_INVALID.  The client
  // code can choose to move a handle out to be owned separately by setting
  // the handle field to ZX_HANDLE_INVALID (or leaving it 0 which is the same
  // thing).
  ~FidlStruct() { reset_internal(nullptr); }

  void reset(const FidlCStruct* to_copy_and_own_handles) {
    ZX_DEBUG_ASSERT_COND(!is_moved_out_);
    reset_internal(to_copy_and_own_handles);
  }

  // Stop managing the handles, and return a pointer for the caller's
  // convenience.  After this, get() will return nullptr to discourage further
  // use of non-owned handle fields.
  //
  // The caller must stop using the returned value before the earlier of when
  // this instance is deleted or when this instance is re-used.
  FidlCStruct* release() {
    ZX_DEBUG_ASSERT_COND(!is_moved_out_);
    ZX_DEBUG_ASSERT(ptr_);
    FidlCStruct* tmp = ptr_;
    ptr_ = nullptr;
    return tmp;
  }

  // Return value can be nullptr if release() has been called previously.
  FidlCStruct* get() {
    ZX_DEBUG_ASSERT_COND(!is_moved_out_);
    return ptr_;
  }

  const FidlCStruct* get() const {
    ZX_DEBUG_ASSERT_COND(!is_moved_out_);
    return ptr_;
  }

  bool is_valid() {
    ZX_DEBUG_ASSERT_COND(!is_moved_out_);
    return !!ptr_;
  }

  operator bool() const {
    ZX_DEBUG_ASSERT_COND(!is_moved_out_);
    return !!ptr_;
  }

  FidlCStruct* operator->() {
    ZX_DEBUG_ASSERT_COND(!is_moved_out_);
    ZX_DEBUG_ASSERT(ptr_);
    return ptr_;
  }

  FidlCStruct& operator*() {
    ZX_DEBUG_ASSERT_COND(!is_moved_out_);
    ZX_DEBUG_ASSERT(ptr_);
    return *ptr_;
  }

  // transfer handle ownership, copy the data, invalidate the source
  FidlStruct(FidlStruct&& to_move) {
    reset(to_move.release_allow_null());
#if ZX_DEBUG_ASSERT_IMPLEMENTED
    to_move.is_moved_out_ = true;
#endif
  }

  // transfer handle ownership, copy the data, invalidate the source
  FidlStruct& operator=(FidlStruct&& to_move) {
    reset(to_move.release_allow_null());
#if ZX_DEBUG_ASSERT_IMPLEMENTED
    to_move.is_moved_out_ = true;
#endif
    return *this;
  }

  FidlLlcppStruct TakeAsLlcpp() {
    static_assert(!fidl::TypeTraits<FidlLlcppStruct>::kHasPointer);
    constexpr size_t kSize = sizeof(FidlLlcppStruct);
    ZX_DEBUG_ASSERT(*this);
    // un-manage handles
    FidlCStruct* tmp = release();
    FidlLlcppStruct result;
    static_assert(sizeof(*tmp) == sizeof(result));
    memcpy(&result, tmp, kSize);
    // handles now managed by result
    return result;
  }

  // if (ProvideCopyAsLlcpp_v)
  //   FidlLlcppStruct CopyAsLlcpp() {...}
  template <typename DummyFalse = std::false_type>
  std::enable_if_t<DummyFalse::value || ProvideCopyAsLlcpp_v, FidlLlcppStruct> CopyAsLlcpp() {
    static_assert(std::is_same_v<DummyFalse, std::false_type>, "don't override DummyFalse");
    static_assert(!fidl::TypeTraits<FidlLlcppStruct>::kHasPointer);
    static_assert(fidl::TypeTraits<FidlLlcppStruct>::kMaxNumHandles == 0);
    ZX_DEBUG_ASSERT(*this);
    FidlLlcppStruct result;
    static_assert(sizeof(*get()) == sizeof(result));
    memcpy(&result, get(), sizeof(result));
    // only data - no handles to manage
    return result;
  }

  // mutable borrow
  FidlLlcppStruct& BorrowAsLlcpp() {
    ZX_DEBUG_ASSERT(*this);
    return *reinterpret_cast<FidlLlcppStruct*>(ptr_);
  }

  // immutable borrow
  const FidlLlcppStruct& BorrowAsLlcpp() const {
    ZX_DEBUG_ASSERT(*this);
    return *reinterpret_cast<FidlLlcppStruct*>(ptr_);
  }

  // mutable borrow without ever owning - nullptr to_borrow is fine
  static FidlLlcppStruct* BorrowAsLlcpp(FidlCStruct* to_borrow) {
    ZX_DEBUG_ASSERT(sizeof(FidlLlcppStruct) == sizeof(*to_borrow));
    return reinterpret_cast<FidlLlcppStruct*>(to_borrow);
  }

  // immutable borrow without ever owning - nullptr to_borrow is fine
  static const FidlLlcppStruct* BorrowAsLlcpp(const FidlCStruct* to_borrow) {
    ZX_DEBUG_ASSERT(sizeof(FidlLlcppStruct) == sizeof(*to_borrow));
    return reinterpret_cast<const FidlLlcppStruct*>(to_borrow);
  }

 private:
  void reset_internal(const FidlCStruct* to_copy_and_own_handles) {
    if (ptr_) {
      // Move handles into the owned natural type, then discard the value.
      (void)fidl::ToNatural(TakeAsLlcpp());
    }
    if (to_copy_and_own_handles) {
      storage_ = *to_copy_and_own_handles;
      ptr_ = &storage_;
    } else {
      ptr_ = nullptr;
    }
  }

  // Same as release, but don't assert on ptr_. This allows moving from a null
  // struct.
  FidlCStruct* release_allow_null() {
    ZX_DEBUG_ASSERT_COND(!is_moved_out_);
    FidlCStruct* tmp = ptr_;
    ptr_ = nullptr;
    return tmp;
  }

  FidlCStruct storage_{};
  FidlCStruct* ptr_ = nullptr;

#if ZX_DEBUG_ASSERT_IMPLEMENTED
  bool is_moved_out_ = false;
#endif

  FidlStruct(const FidlStruct& to_copy) = delete;
  FidlStruct& operator=(const FidlStruct& to_copy) = delete;
};

#endif  // LIB_FIDL_ASYNC_2_FIDL_STRUCT_H_
