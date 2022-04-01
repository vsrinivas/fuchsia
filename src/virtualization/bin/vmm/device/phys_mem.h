// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_PHYS_MEM_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_PHYS_MEM_H_

#include <lib/stdcompat/span.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

// For devices that can have their addresses anywhere we run a dynamic
// allocator that starts fairly high in the guest physical address space.
constexpr zx_gpaddr_t kFirstDynamicDeviceAddr = 0xb00000000;

// Arbitrarily large number used when restricting guest memory ranges. If a restricted range
// has this size, it means "restrict from the base address until +INF".
constexpr uint64_t kGuestMemoryAllRemainingRange = 1ul << 52;

struct GuestMemoryRegion {
  // Base address of a region of guest physical address space.
  zx_gpaddr_t base;
  // Size of a region of guest physical address space in bytes.
  uint64_t size;

  constexpr static bool CompareMinByBase(const GuestMemoryRegion& lhs,
                                         const GuestMemoryRegion& rhs) {
    return lhs.base < rhs.base;
  }
};

class PhysMem {
 public:
  // Initializes this PhysMem object with all of guest memory mapped into a child VMAR.
  zx_status_t Init(zx::vmo vmo);

  // Initializes this PhysMem object with only valid guest memory regions mapped into a child VMAR.
  zx_status_t Init(const std::vector<GuestMemoryRegion>& guest_mem, zx::vmo vmo);

  ~PhysMem();

  const zx::vmo& vmo() const { return vmo_; }
  size_t size() const { return vmo_size_; }

  // Requests a pointer to the guest memory at the given offset, valid for the
  // given number of bytes.
  //
  // It is the caller's responsibility to only request offsets that are a
  // multiple of the object's alignment, otherwise this function will panic. If
  // alignment cannot be known you should either use `ptr` and copy in/out as
  // raw bytes, or use `read`, modify, and then `write`.
  template <typename T>
  T* aligned_as(zx_vaddr_t off, size_t bytes = sizeof(T)) const {
    FX_CHECK(off + bytes >= off && off + bytes <= vmo_size_)
        << "Region is outside of guest physical memory";
    FX_CHECK(((addr_ + off) % alignof(T)) == 0) << "Offset would not be aligned";
    return reinterpret_cast<T*>(addr_ + off);
  }

  // Requests a raw pointer to the guest memory at the given offset, valid for
  // the given number of bytes.
  void* ptr(zx_vaddr_t off, size_t bytes) const {
    FX_CHECK(off + bytes >= off && off + bytes <= vmo_size_)
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
  zx_vaddr_t offset(T* ptr, size_t bytes = sizeof(T)) const {
    zx_vaddr_t off = reinterpret_cast<zx_vaddr_t>(ptr);
    FX_CHECK(off + bytes >= off && off + bytes >= addr_ && (off + bytes - addr_ <= vmo_size_))
        << "Pointer is not contained within guest physical memory";
    return off - addr_;
  }

  // Requests a span covering the given range of memory.
  template <typename T>
  cpp20::span<T> span(zx_vaddr_t off, size_t count) const {
    return cpp20::span<T>(aligned_as<T>(off, sizeof(T) * count), count);
  }

 protected:
  zx::vmo vmo_;
  size_t vmo_size_ = 0;
  zx_vaddr_t addr_ = 0;
  zx::vmar child_vmar_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_PHYS_MEM_H_
