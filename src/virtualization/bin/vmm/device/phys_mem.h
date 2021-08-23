// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_PHYS_MEM_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_PHYS_MEM_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/vmo.h>

class PhysMem {
 public:
  zx_status_t Init(zx::vmo vmo);

  ~PhysMem();

  const zx::vmo& vmo() const { return vmo_; }
  size_t size() const { return vmo_size_; }

  // Requests a pointer to the guest memory at the given offset, valid for the given length.
  // It is the callers responsibility to only request offsets that are a multiple of the object's
  // alignment, and this method will panic otherwise. If alignment cannot be known you should
  // either use `ptr` and copy in/out as raw bytes, or use `read`, modify, and then `write`.
  template <typename T>
  T* aligned_as(zx_vaddr_t off, size_t len = sizeof(T)) const {
    FX_CHECK(off + len >= off && off + len <= vmo_size_)
        << "Region is outside of guest physical memory";
    FX_CHECK(((addr_ + off) % alignof(T)) == 0) << "Offset would not be aligned";
    return reinterpret_cast<T*>(addr_ + off);
  }

  // Requests a raw pointer to the guest memory at the given offset, valid for the given length.
  void* ptr(zx_vaddr_t off, size_t len) const {
    FX_CHECK(off + len >= off && off + len <= vmo_size_)
        << "Region is outside of guest physical memory";
    return reinterpret_cast<void*>(addr_ + off);
  }

  // Read an object of type T from the guest memory.
  // This should only be used to read PoD objects.
  template <typename T>
  T read(zx_vaddr_t off) const {
    static_assert(std::is_trivially_copyable_v<T>);
    T temp;
    memcpy(&temp, ptr(off, sizeof(T)), sizeof(T));
    return temp;
  }

  // Write an object of type T to the guest memory.
  // This should only be used to write PoD objects.
  template <typename T>
  void write(zx_vaddr_t off, const T& val) const {
    static_assert(std::is_trivially_copyable_v<T>);
    memcpy(ptr(off, sizeof(T)), &val, sizeof(T));
  }

  template <typename T>
  zx_vaddr_t offset(T* ptr, size_t len = sizeof(T)) const {
    zx_vaddr_t off = reinterpret_cast<zx_vaddr_t>(ptr);
    FX_CHECK(off + len >= off && off + len >= addr_ && (off + len - addr_ <= vmo_size_))
        << "Pointer is not contained within guest physical memory";
    return off - addr_;
  }

 protected:
  zx::vmo vmo_;
  size_t vmo_size_ = 0;
  zx_vaddr_t addr_ = 0;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_PHYS_MEM_H_
