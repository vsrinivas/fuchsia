// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// An RAII wrapper around ACPI_BUFFER to handle memory ownership and allow easy iteration.
#ifndef SRC_DEVICES_BOARD_LIB_ACPI_UTIL_H_
#define SRC_DEVICES_BOARD_LIB_ACPI_UTIL_H_

#include <algorithm>
#include <memory>

#include <acpica/acpi.h>

namespace acpi {
namespace internal {

static inline uint32_t ExtractPnpIdWord(const ACPI_PNP_DEVICE_ID& id, size_t offset) {
  auto buf = reinterpret_cast<const unsigned char*>(id.String);
  auto buf_len = static_cast<size_t>(id.Length);

  if (offset >= buf_len) {
    return 0;
  }

  size_t i;
  size_t avail = buf_len - offset;
  uint32_t ret = buf[offset];
  for (i = 1; i < std::min(avail, sizeof(uint32_t)); ++i) {
    ret = (ret << 8) | buf[offset + i];
  }
  ret <<= (sizeof(uint32_t) - i) * 8;

  return ret;
}

}  // namespace internal

// AcpiBuffer provides RAII memory management for ACPI_BUFFERs as well as range
// based iterators for a specified ACPI_BUFFER type T.
template <class T>
class AcpiBuffer : public ACPI_BUFFER {
 public:
  AcpiBuffer() : ACPI_BUFFER{ACPI_ALLOCATE_BUFFER, nullptr} {}
  AcpiBuffer(const AcpiBuffer&& acpi_buffer) noexcept : ACPI_BUFFER(std::move(acpi_buffer)) {}
  AcpiBuffer(ACPI_SIZE length, void* pointer) : ACPI_BUFFER{length, pointer} {}
  AcpiBuffer(const AcpiBuffer&) = delete;
  ~AcpiBuffer() { AcpiOsFree(Pointer); }

  class iterator {
   public:
    explicit iterator(ACPI_BUFFER buffer)
        : length_(buffer.Length), pointer_(static_cast<T*>(buffer.Pointer)) {}
    iterator() : pointer_(nullptr) {}
    T& operator*() { return *pointer_; }
    friend bool operator==(const AcpiBuffer::iterator& ai, const AcpiBuffer::iterator& bi) {
      return ai.pointer_ == bi.pointer_;
    }
    friend bool operator!=(const AcpiBuffer::iterator& ai, const AcpiBuffer::iterator& bi) {
      return !(ai == bi);
    }
    iterator& operator++() {
      length_ -= pointer_->Length;
      pointer_ = reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(pointer_) + pointer_->Length);

      // If we've reached ACPI's end of list node then return the end() sentinel
      if (length_ == 0 || pointer_->Length == 0) {
        pointer_ = nullptr;
      }
      return *this;
    }

   private:
    ACPI_SIZE length_ = 0;
    T* pointer_;
  };

  auto begin() const { return AcpiBuffer::iterator(*this); }
  auto end() const { return AcpiBuffer::iterator(); }
};

// An RAII unique pointer type for resources allocated from the ACPICA library.
template <typename T>
struct UniquePtrDeleter {
  void operator()(T* mem) { ACPI_FREE(mem); }
};

template <typename T>
using UniquePtr = std::unique_ptr<T, UniquePtrDeleter<T>>;

}  // namespace acpi

#endif  // SRC_DEVICES_BOARD_LIB_ACPI_UTIL_H_
