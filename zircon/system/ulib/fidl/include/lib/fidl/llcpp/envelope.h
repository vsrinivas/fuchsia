// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_ENVELOPE_H_
#define LIB_FIDL_LLCPP_ENVELOPE_H_

#include <lib/fidl/llcpp/object_view.h>
#include <zircon/fidl.h>

namespace fidl {

// Envelope is a typed version of fidl_envelope_t and represents the in-memory
// structure of LLCPP envelopes.
// Envelope has three template specializations:
// - sizeof(T) > 4 - Out-of-line - envelope is a pointer to out-of-line data.
// - sizeof(T) <= 4 - Inline - envelope contains the value within its body.
// - T == void - used in unions to hold data until it is cast to one of the
//   other types.
template <typename T, typename = void>
class Envelope {
 public:
  bool has_data() const { return data_ != nullptr; }
  const T& get_data() const { return *data_; }
  T& get_data() { return *data_; }
  void set_data(ObjectView<T> value) { data_ = std::move(value); }
  void clear_data() { data_ = nullptr; }

 private:
  ObjectView<T> data_;
};

// This definition of Envelope is for inline data.
// To maintain the existing interface for unions and tables, bytes are copied into
// the inline value rather than storing it natively.
template <typename T>
class Envelope<T, std::enable_if_t<sizeof(T) <= FIDL_ENVELOPE_INLINING_SIZE_THRESHOLD>> {
 public:
  bool has_data() const { return (flags_ & FIDL_ENVELOPE_FLAGS_INLINING_MASK) != 0; }
  const T& get_data() const {
    ZX_ASSERT(has_data());
    return inline_value_;
  }
  T& get_data() {
    ZX_ASSERT(has_data());
    return inline_value_;
  }
  void set_data(ObjectView<T> value) {
    if (value != nullptr) {
      set_data(std::move(*value));
    } else {
      clear_data();
    }
  }
  void set_data(T value) {
    inline_value_ = std::move(value);
    num_handles_ = (ContainsHandle<T>::value &&
                    reinterpret_cast<zx_handle_t&>(inline_value_) != ZX_HANDLE_INVALID)
                       ? 1
                       : 0;
    flags_ |= FIDL_ENVELOPE_FLAGS_INLINING_MASK;
  }
  void clear_data() {
    // Assign zero to all envelope fields, making this the zero envelope.
    // T{} is guaranteed to have zeroed bytes.
    // - primitive types - it is trivially zero.
    // - handle types - initialized to ZX_INVALID_HANDLE.
    inline_value_ = T{};
    num_handles_ = 0;
    flags_ = 0;
  }

 private:
  T inline_value_ = {};
  alignas(4) [[maybe_unused]] uint16_t num_handles_ = 0;
  uint16_t flags_ = 0;
};

// Used in unions to represent an untyped envelope before it is cast to a typed
// envelope.
class UntypedEnvelope {
 public:
  template <typename T>
  const Envelope<T>& As() const {
    return *reinterpret_cast<const Envelope<T>*>(this);
  }
  template <typename T>
  Envelope<T>& As() {
    return *reinterpret_cast<Envelope<T>*>(this);
  }

 private:
  [[maybe_unused]] uint64_t unused_ = 0;
};

static_assert(sizeof(Envelope<uint8_t>) == sizeof(fidl_envelope_v2_t),
              "Envelope<T> must have the same size as fidl_envelope_v2_t");
static_assert(sizeof(Envelope<uint64_t>) == sizeof(fidl_envelope_v2_t),
              "Envelope<T> must have the same size as fidl_envelope_v2_t");
static_assert(sizeof(UntypedEnvelope) == sizeof(fidl_envelope_v2_t),
              "UntypedEnvelope must have the same size as fidl_envelope_v2_t");

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_ENVELOPE_H_
