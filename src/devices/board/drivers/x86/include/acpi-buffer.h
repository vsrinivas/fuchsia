// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// An RAII wrapper around ACPI_BUFFER to handle memory ownership and allow easy iteration.
#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ACPI_BUFFER_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ACPI_BUFFER_H_

#include <acpica/acpi.h>

namespace acpi {

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
    bool operator==(const AcpiBuffer::iterator& ai) { return pointer_ == ai.pointer_; }
    bool operator!=(const AcpiBuffer::iterator& ai) { return !(*this == ai); }
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

}  // namespace acpi

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ACPI_BUFFER_H_
