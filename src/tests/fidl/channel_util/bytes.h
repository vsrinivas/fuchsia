// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines a GIDL-like DSL in C++ to help with defining FIDL
// payload bytes.

#ifndef SRC_TESTS_FIDL_CHANNEL_UTIL_BYTES_H_
#define SRC_TESTS_FIDL_CHANNEL_UTIL_BYTES_H_

#include <lib/fidl/cpp/natural_types.h>
#include <lib/fidl/cpp/transaction_header.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>

#include <array>
#include <utility>
#include <vector>

namespace channel_util {

class Bytes {
 public:
  Bytes() = default;
  Bytes(Bytes&&) = default;
  Bytes(const Bytes&) = default;
  Bytes& operator=(Bytes&&) = default;
  Bytes& operator=(const Bytes&) = default;

  // NOLINTNEXTLINE
  Bytes(uint8_t value) { data_.push_back(value); }
  Bytes(std::initializer_list<Bytes> bytes) {
    // Flatten the input.
    for (Bytes b : bytes) {
      data_.insert(data_.end(), b.data_.begin(), b.data_.end());
    }
  }
  template <typename IterType>
  Bytes(IterType first, IterType last) : data_(first, last) {}

  size_t size() const { return data_.size(); }
  const uint8_t* data() const { return data_.data(); }
  uint8_t* data() { return data_.data(); }

  const std::vector<uint8_t>& as_vec() const { return data_; }
  std::vector<uint8_t>& as_vec() { return data_; }

 private:
  std::vector<uint8_t> data_;
};

inline bool operator==(const Bytes& a, const Bytes& b) {
  if (&a == &b) {
    return true;
  }
  if (a.size() != b.size()) {
    return false;
  }
  return 0 == memcmp(a.data(), b.data(), a.size());
}
inline bool operator!=(const Bytes& a, const Bytes& b) { return !(a == b); }

template <typename T>
inline Bytes as_bytes(const T& value) {
  static_assert(std::has_unique_object_representations_v<T>,
                "objects with internal padding disallowed as the padding bytes are undefined");
  std::array<uint8_t, sizeof(T)> bytes;
  memcpy(bytes.data(), &value, sizeof(T));
  return Bytes(bytes.begin(), bytes.end());
}

inline Bytes u8(uint8_t value) { return as_bytes(value); }
inline Bytes u16(uint16_t value) { return as_bytes(value); }
inline Bytes u32(uint32_t value) { return as_bytes(value); }
inline Bytes u64(uint64_t value) { return as_bytes(value); }
inline Bytes i8(int8_t value) { return as_bytes(value); }
inline Bytes i16(int16_t value) { return as_bytes(value); }
inline Bytes i32(int32_t value) { return as_bytes(value); }
inline Bytes i64(int64_t value) { return as_bytes(value); }

class RepeatOp {
 public:
  explicit RepeatOp(uint8_t byte) : byte_(byte) {}

  Bytes times(size_t count) const {
    std::vector<uint8_t> result;
    for (size_t i = 0; i < count; i++) {
      result.push_back(byte_);
    }
    return Bytes(result.begin(), result.end());
  }

 private:
  uint8_t byte_;
};

inline RepeatOp repeat(uint8_t byte) { return RepeatOp(byte); }

inline Bytes padding(size_t count) { return repeat(0).times(count); }

inline Bytes header(zx_txid_t txid, uint64_t ordinal, fidl::MessageDynamicFlags flags) {
  fidl_message_header_t hdr;
  fidl::InitTxnHeader(&hdr, txid, ordinal, flags);
  return as_bytes(hdr);
}

template <typename FidlType>
inline Bytes encode(FidlType message) {
  auto result = fidl::Encode(message);
  ZX_ASSERT_MSG(result.message().ok(), "encode failed");
  ZX_ASSERT_MSG(result.message().handle_actual() == 0u, "message contained handles");
  auto copied_bytes = result.message().CopyBytes();
  return Bytes(copied_bytes.data(), copied_bytes.data() + copied_bytes.size());
}

inline Bytes handle_present() { return repeat(0xff).times(4); }
inline Bytes handle_absent() { return repeat(0x00).times(4); }

inline Bytes pointer_present() { return repeat(0xff).times(8); }
inline Bytes pointer_absent() { return repeat(0x00).times(8); }

inline Bytes union_ordinal(fidl_xunion_tag_t ordinal) { return u64(ordinal); }
inline Bytes table_max_ordinal(uint64_t ordinal) { return u64(ordinal); }
inline Bytes string_length(uint64_t length) { return u64(length); }
inline Bytes vector_length(uint64_t length) { return u64(length); }

inline Bytes transport_err_unknown_method() { return i32(ZX_ERR_NOT_SUPPORTED); }

inline Bytes string_header(uint64_t length) {
  return {
      string_length(length),
      pointer_present(),
  };
}
inline Bytes vector_header(uint64_t length) {
  return {
      vector_length(length),
      pointer_present(),
  };
}

inline Bytes out_of_line_envelope(uint16_t num_bytes, uint8_t num_handles) {
  return {u32(num_bytes), u16(num_handles), u16(0)};
}
inline Bytes inline_envelope(const Bytes& value, bool has_handles) {
  ZX_ASSERT_MSG(value.size() <= 4, "inline envelope values are <= 4 bytes in size");
  return {value, padding(4 - value.size()), u16(has_handles), u16(1)};
}

}  // namespace channel_util

#endif  // SRC_TESTS_FIDL_CHANNEL_UTIL_BYTES_H_
