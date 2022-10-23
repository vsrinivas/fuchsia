// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_BINARY_DECODER_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_BINARY_DECODER_H_

#include <lib/stdcompat/span.h>
#include <lib/zx/result.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <cstdint>
#include <type_traits>

#include <fbl/string.h>

#include "debug-logging.h"

namespace audio::intel_hda {

// Light-weight class for decoding packed structs in a safe manner.
//
// Each operation returns either a specified struct or an error
// indicating that the read would go out of bounds of the original input
// buffer. Successful reads consume bytes from the buffer, while failed
// reads don't modify internal state.
//
// The code avoids performing unaligned reads.
//
// A typical use will be as follows:
//
//   // Give the decoder a reference to binary data.
//   BinaryDecoder decoder(cpp20::span<const uint8_t>(...));
//
//   // Read some structures.
//   StatusOr<int32_t> a = decoder.Read<int32_t>();
//   StatusOr<MyStruct> b = decoder.Read<MyStruct>();
//   StatusOr<OtherStruct> c = decoder.Read<OtherStruct>();
//
//   // Read off a range of 16 bytes.
//   StatusOr<cpp20::span<const uint8_t>> bytes = decoder.Read(16);
//
//   // Read off a struct that encodes its length as a field.
//   //
//   // The result will consist of a "MyStruct" and additional payload data
//   // as a byte range.
//   StatusOr<std::tuple<MyStruct, cpp20::span<const uint8_t>>> data
//       = decoder.VariableLengthRead<MyStruct>(&MyStruct::length);
//
class BinaryDecoder {
 public:
  explicit BinaryDecoder(cpp20::span<const uint8_t> data) : buffer_(data) {}

  // Read off the given number of bytes from the beginning of the buffer.
  zx::result<cpp20::span<const uint8_t>> Read(size_t size) {
    if (size > buffer_.size_bytes()) {
      GLOBAL_LOG(DEBUG,
                 "Input data has been truncated: expected %ld bytes, but only %ld available.", size,
                 buffer_.size_bytes());
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
    cpp20::span<const uint8_t> result = buffer_.subspan(0, size);
    buffer_ = buffer_.subspan(size);
    return zx::ok(result);
  }

  // Fetch a structure of type |T| from the buffer, and write it to |result|.
  //
  // |result| will contain unpacked data iff Status is ok.
  //
  // |T| should be a POD type that can be initialized via memcpy().
  template <typename T>
  zx_status_t Read(T* result) {
    static_assert(std::is_pod<T>::value, "Function only supports POD types.");
    zx::result<cpp20::span<const uint8_t>> buffer = Read(sizeof(T));
    if (!buffer.is_ok()) {
      return buffer.status_value();
    }
    cpp20::span<const uint8_t> bytes = std::move(buffer.value());
    ZX_DEBUG_ASSERT(bytes.size_bytes() == sizeof(T));
    memcpy(result, bytes.data(), sizeof(T));
    return ZX_OK;
  }

  // Fetch a structure of type |T| from the buffer.
  //
  // |T| should be a POD type that can be initialized via memcpy().
  template <typename T>
  zx::result<T> Read() {
    T result;
    zx_status_t status = Read(&result);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(std::move(result));
  }

  // Fetch a variable-length structure of type |T| which is followed by
  // some number of bytes, specified by a field |F|.
  //
  // |T| should be a POD type that can be initialized via memcpy().
  //
  // |T::length_field| must be castable to size_t.
  template <typename T, typename F>
  zx::result<std::tuple<T, cpp20::span<const uint8_t>>> VariableLengthRead(F T::*length_field) {
    cpp20::span<const uint8_t> orig_buffer = buffer_;

    // Read header.
    auto buffer = Read<T>();
    if (!buffer.is_ok()) {
      return zx::error(buffer.status_value());
    }
    const auto& header = buffer.value();
    // Get the |length| field, and ensure that it covers at least the size
    // of the header.
    auto length = static_cast<size_t>(header.*length_field);
    if (length < sizeof(T)) {
      // Operations should be atomic: if we fail the second read, restore the
      // original buffer.
      buffer_ = orig_buffer;
      GLOBAL_LOG(DEBUG,
                 "Length field shorter than structure type: length field specified as %lu bytes, "
                 "but structure is %lu bytes.",
                 length, sizeof(T));
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }

    // Read the rest of the payload.
    auto additional_bytes = Read(length - sizeof(T));
    if (!additional_bytes.is_ok()) {
      return zx::error(additional_bytes.status_value());
    }
    return zx::ok(std::make_tuple(header, additional_bytes.value()));
  }

 private:
  cpp20::span<const uint8_t> buffer_;
};

// Parse a string in an array, where the string may either:
//
//   * Be NUL terminated; or
//   * Take up all the elements of the array, and have no NUL termination.
//
template <size_t BufferSize>
fbl::String ParseUnpaddedString(const char (&s)[BufferSize]) {
  return fbl::String(s, strnlen(s, BufferSize));
}
template <size_t BufferSize>
fbl::String ParseUnpaddedString(const uint8_t (&s)[BufferSize]) {
  return ParseUnpaddedString(reinterpret_cast<const char(&)[BufferSize]>(s));
}

}  // namespace audio::intel_hda

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_BINARY_DECODER_H_
