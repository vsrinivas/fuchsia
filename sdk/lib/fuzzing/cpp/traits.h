// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FUZZING_CPP_TRAITS_H_
#define LIB_FUZZING_CPP_TRAITS_H_

#include <lib/fidl/cpp/interface_request.h>
#include <lib/fuzzing/cpp/fuzz_input.h>
#include <lib/zx/object.h>
#include <zircon/assert.h>
#include <zircon/system/ulib/zx/include/lib/zx/bti.h>
#include <zircon/system/ulib/zx/include/lib/zx/channel.h>
#include <zircon/system/ulib/zx/include/lib/zx/debuglog.h>
#include <zircon/system/ulib/zx/include/lib/zx/event.h>
#include <zircon/system/ulib/zx/include/lib/zx/eventpair.h>
#include <zircon/system/ulib/zx/include/lib/zx/exception.h>
#include <zircon/system/ulib/zx/include/lib/zx/fifo.h>
#include <zircon/system/ulib/zx/include/lib/zx/guest.h>
#include <zircon/system/ulib/zx/include/lib/zx/interrupt.h>
#include <zircon/system/ulib/zx/include/lib/zx/iommu.h>
#include <zircon/system/ulib/zx/include/lib/zx/pager.h>
#include <zircon/system/ulib/zx/include/lib/zx/pmt.h>
#include <zircon/system/ulib/zx/include/lib/zx/port.h>
#include <zircon/system/ulib/zx/include/lib/zx/profile.h>
#include <zircon/system/ulib/zx/include/lib/zx/resource.h>
#include <zircon/system/ulib/zx/include/lib/zx/socket.h>
#include <zircon/system/ulib/zx/include/lib/zx/suspend_token.h>
#include <zircon/system/ulib/zx/include/lib/zx/timer.h>
#include <zircon/system/ulib/zx/include/lib/zx/vcpu.h>
#include <zircon/system/ulib/zx/include/lib/zx/vmar.h>
#include <zircon/system/ulib/zx/include/lib/zx/vmo.h>
#include <zircon/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fuzzing {

// Abstract type traits for low-level types needed by FIDL fuzzers:
//
// size_t MinSize<T>()
// Returns the minimum size, in bytes, of fuzz input data consumed by an
// instance of type, T.
//
// T Allocate<T>(FuzzInput* src, size_t* size)
// Allocates at most |size| bytes from |src| to return a fuzzer input of type,
// T. Modifies |size| to reflect actual number of bytes consumed from |src|.
// Both |src| and |size| are assumed to be non-null.

template <typename T>
struct MinSize {
  static_assert(sizeof(T) == -1, "Missing fidl::fuzzing::MinSize<T> specialization");

  constexpr operator size_t() const { return 0; }
};

template <typename T>
struct Allocate {
  static_assert(sizeof(T) == -1, "Missing fidl::fuzzing::Allocate<T> specialization");

  T operator()(FuzzInput* src, size_t* size) {
    *size = 0;
    return T();
  }
};

// Statically sized (primitive) traits operate as follows:
// 1. MinSize<T> = sizeof(T);
// 2. Allocate<T>(src, size) asserts that |size| >= sizeof(T);
// 3. Allocate<T>(src, size) asserts that taking sufficient bytes for return
//    succeeds.
// 4. Allocate<T>(src, size) takes up to |size| bytes to produce a new fuzzer
//    input of type, T.
#define FUZZING_STATIC(T)                                   \
  template <>                                               \
  struct MinSize<T> {                                       \
    constexpr operator size_t() const { return sizeof(T); } \
  };                                                        \
  template <>                                               \
  struct Allocate<T> {                                      \
    T operator()(FuzzInput* src, size_t* size) {            \
      T out;                                                \
      ZX_ASSERT(*size >= sizeof(T));                        \
      *size = sizeof(T);                                    \
      ZX_ASSERT(src->CopyObject(&out));                     \
      return out;                                           \
    }                                                       \
  }

// TODO(fxbug.dev/25053): Use a more useful distribution for types such as bool.
FUZZING_STATIC(bool);
FUZZING_STATIC(uint8_t);
FUZZING_STATIC(uint16_t);
FUZZING_STATIC(uint32_t);
FUZZING_STATIC(uint64_t);
FUZZING_STATIC(int8_t);
FUZZING_STATIC(int16_t);
FUZZING_STATIC(int32_t);
FUZZING_STATIC(int64_t);
FUZZING_STATIC(float);
FUZZING_STATIC(double);

#undef FUZZING_STATIC

// Handle traits:
// Like FUZZING_STATIC(zx_handle_t), but return ::zx::object<T> instance.
//
// TODO(fxbug.dev/25053): Generate a distribution of legitimate and illegitimate handles.
template <typename T>
struct MinSize<::zx::object<T>> {
  constexpr operator size_t() const { return sizeof(zx_handle_t); }
};
template <typename T>
struct Allocate<::zx::object<T>> {
  ::zx::object<T> operator()(FuzzInput* src, size_t* size) {
    zx_handle_t handle;
    ZX_ASSERT(*size >= sizeof(zx_handle_t));
    *size = sizeof(zx_handle_t);
    ZX_ASSERT(src->CopyObject(&handle));

    return ::zx::object<T>(handle);
  }
};

// Statically sized zircon object traits delegate to ::zx::object<T> traits.
#define FUZZING_ZX_OBJ(T)                                                    \
  template <>                                                                \
  struct MinSize<T> {                                                        \
    constexpr operator size_t() const { return MinSize<::zx::object<T>>(); } \
  };                                                                         \
  template <>                                                                \
  struct Allocate<T> {                                                       \
    T operator()(FuzzInput* src, size_t* size) {                             \
      return T(Allocate<zx::object<T>>{}(src, size).get());                  \
    }                                                                        \
  }

FUZZING_ZX_OBJ(::zx::bti);
FUZZING_ZX_OBJ(::zx::channel);
FUZZING_ZX_OBJ(::zx::debuglog);
FUZZING_ZX_OBJ(::zx::event);
FUZZING_ZX_OBJ(::zx::eventpair);
FUZZING_ZX_OBJ(::zx::exception);
FUZZING_ZX_OBJ(::zx::fifo);
FUZZING_ZX_OBJ(::zx::guest);
FUZZING_ZX_OBJ(::zx::interrupt);
FUZZING_ZX_OBJ(::zx::iommu);
FUZZING_ZX_OBJ(::zx::pager);
FUZZING_ZX_OBJ(::zx::pmt);
FUZZING_ZX_OBJ(::zx::port);
FUZZING_ZX_OBJ(::zx::profile);
FUZZING_ZX_OBJ(::zx::resource);
FUZZING_ZX_OBJ(::zx::socket);
FUZZING_ZX_OBJ(::zx::suspend_token);
FUZZING_ZX_OBJ(::zx::timer);
FUZZING_ZX_OBJ(::zx::vcpu);
FUZZING_ZX_OBJ(::zx::vmar);
FUZZING_ZX_OBJ(::zx::vmo);

#undef FUZZING_ZX_OBJ

template <typename T>
struct MinSize<::fidl::InterfaceRequest<T>> {
  constexpr operator size_t() const { return MinSize<::zx::channel>(); }
};

template <typename T>
struct Allocate<::fidl::InterfaceRequest<T>> {
  ::fidl::InterfaceRequest<T> operator()(FuzzInput* src, size_t* size) {
    return ::fidl::InterfaceRequest<T>(std::move(Allocate<zx::channel>{}(src, size)));
  }
};

// String traits:
// MinSize is 0; take bytes as |const char*| to back |size|-sized string.
template <>
struct MinSize<std::string> {
  constexpr operator size_t() const { return 0; }
};
template <>
struct Allocate<std::string> {
  std::string operator()(FuzzInput* src, size_t* size) {
    if (*size == 0) {
      return std::string();
    }

    const char* out = reinterpret_cast<const char*>(src->TakeBytes(*size));
    return std::string(out, *size);
  }
};

// Array traits:
// MinSize is 0 (i.e., admit empty portion of array); take bytes for up to S
// instances of T. If |size| > S * MinSize<T>(), attempt to evenly distribute
// slack bytes amongst T-instance allocations. The purpose of this distribution
// is to provide data to variable-sized types that may be stored in the array.
//
// Caveat: When MinSize<T>() = 0, attempt to allocate S T-instances.
//
// TODO(fxbug.dev/25053): Consume some input bytes to allocate pseudorandom number of items.
template <typename T, size_t S>
struct MinSize<std::array<T, S>> {
  constexpr operator size_t() const { return 0; }
};
template <typename T, size_t S>
struct Allocate<std::array<T, S>> {
  std::array<T, S> operator()(FuzzInput* src, size_t* size) {
    if (S == 0 || *size == 0 || *size < MinSize<T>()) {
      *size = 0;
      return std::array<T, S>();
    }

    const size_t requested_size = *size;
    const size_t item_size = Allocate<std::array<T, S>>::item_size();
    size_t num;
    size_t slack_per_item;
    if (item_size == 0) {
      // Special case: Items are variable-sized objects that may be empty
      // (e.g., vectors).
      // Attempt to allocate S items.
      num = S;
      slack_per_item = requested_size / num;
    } else {
      // Items are of non-zero size.
      const size_t size_num = requested_size / item_size;
      num = size_num <= S ? size_num : S;
      const size_t slack = requested_size - (num * item_size);
      slack_per_item = num == 0 ? 0 : slack / num;
    }

    std::array<T, S> out;
    *size = 0;
    for (size_t i = 0; i < num; i++) {
      size_t current_item_size = item_size + slack_per_item;
      out[i] = Allocate<T>{}(src, &current_item_size);
      *size += current_item_size;
    }

    return out;
  }

 private:
  static size_t item_size() { return MinSize<T>(); }
};

// Vector traits:
// MinSize is 0 (i.e., admit empty vector); take MinSize<T>()-byte chunks from
// |src| for constructing instances of T. Allocating larger-than-min-size
// T-instances is currently unsupported.
//
// Caveat: When MinSize<T>() = 0, treat T-instances as though they will
// allocate 8 bytes, enough for a 64-bit pointer.
//
// TODO(fxbug.dev/25053): Consume some input bytes to allocate pseudorandom number of items.
template <typename T>
struct MinSize<std::vector<T>> {
  constexpr operator size_t() const { return 0; }
};
template <typename T>
struct Allocate<std::vector<T>> {
  std::vector<T> operator()(FuzzInput* src, size_t* size) {
    const size_t item_size =
        Allocate<std::vector<T>>::item_size() == 0 ? 8 : Allocate<std::vector<T>>::item_size();
    if (*size == 0 || *size < item_size) {
      *size = 0;
      return std::vector<T>();
    }

    const size_t requested_size = *size;
    const size_t num = requested_size / item_size;
    const size_t slack = requested_size - (num * item_size);
    const size_t slack_per_item = num == 0 ? 0 : slack / num;
    std::vector<T> v;
    *size = 0;
    for (size_t i = 0; i < num; i++) {
      size_t current_item_size = item_size + slack_per_item;
      v.push_back(Allocate<T>{}(src, &current_item_size));
      *size += current_item_size;
    }
    return v;
  }

 private:
  static size_t item_size() { return MinSize<T>(); }
};

// Pointer traits:
// Min*size = 0 (i.e., support nullptr); when |size| >= MinSize<T>(), take bytes
// to allocate a non-null std::unique_ptr<T>; otherwise return nullptr.
template <typename T>
struct MinSize<std::unique_ptr<T>> {
  constexpr operator size_t() const { return 0; }
};
template <typename T>
struct Allocate<std::unique_ptr<T>> {
  std::unique_ptr<T> operator()(FuzzInput* src, size_t* size) {
    if (*size < MinSize<T>()) {
      *size = 0;
      return std::unique_ptr<T>();
    }

    return std::make_unique<T>(Allocate<T>{}(src, size));
  }
};

}  // namespace fuzzing

#endif  // LIB_FUZZING_CPP_TRAITS_H_
