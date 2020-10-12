// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_ACPI_LITE_BINARY_READER_H_
#define ZIRCON_KERNEL_LIB_ACPI_LITE_BINARY_READER_H_

#include <lib/acpi_lite/structures.h>
#include <lib/zx/status.h>

#include <fbl/span.h>

namespace acpi_lite {

// Light-weight class for decoding structs in a safe manner.
//
// Each operation returns a pointer to a valid struct or nullptr indicating
// that the read would return an invalid structure, such as a structure out of
// bounds of the original input buffer.
//
// BinaryReader supports a common requirement in ACPI of variable-length
// structures, where a struct consists of a header followed by a payload.
// To support such structures, we require a |size| method returning the
// size of the header + payload.
//
// Successful reads consume bytes from the buffer, while failed reads don't
// modify internal state.
class BinaryReader {
 public:
  BinaryReader() = default;

  // Construct a BinaryReader from the given span.
  explicit BinaryReader(fbl::Span<const uint8_t> data) : buffer_(data) {}

  // Construct a BinaryReader from the given pointer / size pair.
  explicit BinaryReader(const void* data, size_t size)
      : buffer_(reinterpret_cast<const uint8_t*>(data), size) {}

  // Construct a BinaryReader from a valid structure with a size() method.
  template <typename T>
  static BinaryReader FromVariableSizedStruct(const T* header) {
    return BinaryReader(reinterpret_cast<const uint8_t*>(header), header->size());
  }

  // Construct a BinaryReader from a class with a size() method, skipping the header T.
  template <typename T>
  static BinaryReader FromPayloadOfStruct(const T* header) {
    BinaryReader result(reinterpret_cast<const uint8_t*>(header), header->size());
    result.buffer_ = result.buffer_.subspan(sizeof(T));
    return result;
  }

  // Read a fixed-length structure.
  template <typename T>
  const T* ReadFixedLength() {
    static_assert(alignof(T) == 1, "Can only safely read types with alignof(T) == 1.");

    // Ensure we have space.
    if (buffer_.size_bytes() < sizeof(T)) {
      return nullptr;
    }

    // Consume the bytes, and return the struct.
    auto* result = reinterpret_cast<const T*>(buffer_.data());
    buffer_ = buffer_.subspan(sizeof(T));
    return result;
  }

  // Read a variable length structure, where the size is determined by T::size().
  template <typename T>
  const T* Read() {
    static_assert(alignof(T) == 1, "Can only safely read types with alignof(T) == 1.");

    // Read the header.
    if (buffer_.size_bytes() < sizeof(T)) {
      return nullptr;
    }
    auto* ptr = reinterpret_cast<const T*>(buffer_.data());

    // Read the desired size.
    //
    // The reported size must be at least sizeof(T), and must not be larger than
    // the number of bytes we have left in our buffer.
    size_t desired_size = ptr->size();
    if (desired_size < sizeof(T) || desired_size > buffer_.size_bytes()) {
      return nullptr;
    }

    // Consume the bytes, and return the header.
    buffer_ = buffer_.subspan(desired_size);
    return ptr;
  }

  // Discard the given number of bytes.
  //
  // Return true if the bytes could be discarded, or false if there are insufficient bytes.
  bool SkipBytes(size_t bytes) {
    if (buffer_.size() < bytes) {
      return false;
    }
    buffer_ = buffer_.subspan(bytes);
    return true;
  }

  // Return true if all the bytes of the reader have been consumed.
  inline bool empty() const { return buffer_.empty(); }

 private:
  fbl::Span<const uint8_t> buffer_;
};

// Convert a pointer to type |Src| to a pointer of type |Dest|, ensuring that the size of |Src|
// is valid.
//
// We require that the type |Dest| has a field |header| at offset 0 of type |Src|.
template <typename Dest, typename Src>
const Dest* Downcast(const Src* src) {
  static_assert(offsetof(Dest, header) == 0,
                "Expected field |header| to be first field in struct.");
  static_assert(std::is_same_v<decltype(Dest::header), Src>,
                "Expected |Dest::header| type to match |Src|.");
  if (src->size() < sizeof(Dest)) {
    return nullptr;
  }
  return reinterpret_cast<const Dest*>(src);
}

// A "packed" type wraps a plain type, but instructs the compiler to treat it as unaligned data.
template <typename T>
struct Packed {
  T value;
} __PACKED;

}  // namespace acpi_lite

#endif  // ZIRCON_KERNEL_LIB_ACPI_LITE_BINARY_READER_H_
