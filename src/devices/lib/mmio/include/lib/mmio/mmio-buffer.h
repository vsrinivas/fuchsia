// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_MMIO_INCLUDE_LIB_MMIO_MMIO_BUFFER_H_
#define SRC_DEVICES_LIB_MMIO_INCLUDE_LIB_MMIO_MMIO_BUFFER_H_

#include <lib/mmio-ptr/mmio-ptr.h>
#include <lib/mmio/mmio-internal.h>
#include <lib/mmio/mmio-pinned-buffer.h>
#include <zircon/assert.h>
#include <zircon/process.h>

__BEGIN_CDECLS

// Takes raw mmio resources, and maps it into address space. |offset| is the
// offset from the beginning of |vmo| where the mmio region begins. |size|
// specifies the size of the mmio region. |offset| + |size| must be less than
// or equal to the size of |vmo|.
// Always consumes |vmo|, including in error cases.
zx_status_t mmio_buffer_init(mmio_buffer_t* buffer, zx_off_t offset, size_t size, zx_handle_t vmo,
                             uint32_t cache_policy);

// Takes a physical region, and maps it into address space. |base| and |size|
// must be page aligned.
// Callee retains ownership of |resource|.
zx_status_t mmio_buffer_init_physical(mmio_buffer_t* buffer, zx_paddr_t base, size_t size,
                                      zx_handle_t resource, uint32_t cache_policy);

// Unmaps the mmio region.
void mmio_buffer_release(mmio_buffer_t* buffer);

__END_CDECLS

#ifdef __cplusplus

#include <lib/zx/bti.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>

#include <optional>
#include <utility>

namespace fdf {

// Forward declaration.
class MmioView;

// MmioBuffer is wrapper around mmio_block_t.
class MmioBuffer {
 public:
  // DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE
  MmioBuffer(const MmioBuffer&) = delete;
  MmioBuffer& operator=(const MmioBuffer&) = delete;

  MmioBuffer(mmio_buffer_t mmio, const internal::MmioBufferOps* ops = &internal::kDefaultOps,
             const void* ctx = nullptr)
      : mmio_(mmio), ops_(ops), ctx_(ctx) {
    ZX_ASSERT(mmio_.vaddr != nullptr);
  }

  virtual ~MmioBuffer() { mmio_buffer_release(&mmio_); }

  MmioBuffer(MmioBuffer&& other) { transfer(std::move(other)); }

  MmioBuffer& operator=(MmioBuffer&& other) {
    transfer(std::move(other));
    return *this;
  }

  static zx_status_t Create(zx_off_t offset, size_t size, zx::vmo vmo, uint32_t cache_policy,
                            std::optional<MmioBuffer>* mmio_buffer) {
    mmio_buffer_t mmio;
    zx_status_t status = mmio_buffer_init(&mmio, offset, size, vmo.release(), cache_policy);
    if (status == ZX_OK) {
      *mmio_buffer = MmioBuffer(mmio);
    }
    return status;
  }

  static zx_status_t Create(zx_paddr_t base, size_t size, const zx::resource& resource,
                            uint32_t cache_policy, std::optional<MmioBuffer>* mmio_buffer) {
    mmio_buffer_t mmio;
    zx_status_t status = mmio_buffer_init_physical(&mmio, base, size, resource.get(), cache_policy);
    if (status == ZX_OK) {
      *mmio_buffer = MmioBuffer(mmio);
    }
    return status;
  }

  void reset() {
    mmio_buffer_release(&mmio_);
    memset(&mmio_, 0, sizeof(mmio_));
  }

  MMIO_PTR void* get() const { return mmio_.vaddr; }
  zx_off_t get_offset() const { return mmio_.offset; }
  size_t get_size() const { return mmio_.size; }
  zx::unowned_vmo get_vmo() const { return zx::unowned_vmo(mmio_.vmo); }

  zx_status_t Pin(const zx::bti& bti, std::optional<MmioPinnedBuffer>* pinned_buffer) {
    mmio_pinned_buffer_t pinned;
    zx_status_t status = mmio_buffer_pin(&mmio_, bti.get(), &pinned);
    if (status == ZX_OK) {
      *pinned_buffer = MmioPinnedBuffer(pinned);
    }
    return status;
  }

  // Provides a slice view into the mmio.
  // The returned slice object must not outlive this object.
  MmioView View(zx_off_t off) const;
  MmioView View(zx_off_t off, size_t size) const;

  uint32_t ReadMasked32(uint32_t mask, zx_off_t offs) const {
    return ReadMasked<uint32_t>(mask, offs);
  }

  void ModifyBits32(uint32_t bits, uint32_t mask, zx_off_t offs) const {
    ModifyBits<uint32_t>(bits, mask, offs);
  }

  void ModifyBits32(uint32_t val, uint32_t start, uint32_t width, zx_off_t offs) const {
    ModifyBits<uint32_t>(val, start, width, offs);
  }

  void SetBits32(uint32_t bits, zx_off_t offs) const { SetBits<uint32_t>(bits, offs); }

  void ClearBits32(uint32_t bits, zx_off_t offs) const { ClearBits<uint32_t>(bits, offs); }

  void CopyFrom32(const MmioBuffer& source, zx_off_t source_offs, zx_off_t dest_offs,
                  size_t count) const {
    CopyFrom<uint32_t>(source, source_offs, dest_offs, count);
  }

  template <typename T>
  T Read(zx_off_t offs) const {
    if constexpr (sizeof(T) == sizeof(uint8_t)) {
      return Read8(offs);
    } else if constexpr (sizeof(T) == sizeof(uint16_t)) {
      return Read16(offs);
    } else if constexpr (sizeof(T) == sizeof(uint32_t)) {
      return Read32(offs);
    } else if constexpr (sizeof(T) == sizeof(uint64_t)) {
      return Read64(offs);
    } else {
      static_assert(always_false<T>);
    }
  }

  template <typename T>
  T ReadMasked(T mask, zx_off_t offs) const {
    return (Read<T>(offs) & mask);
  }

  template <typename T>
  void CopyFrom(const MmioBuffer& source, zx_off_t source_offs, zx_off_t dest_offs,
                size_t count) const {
    for (size_t i = 0; i < count; i++) {
      T val = source.Read<T>(source_offs);
      Write<T>(val, dest_offs);
      source_offs = source_offs + sizeof(T);
      dest_offs = dest_offs + sizeof(T);
    }
  }

  template <typename T>
  void Write(T val, zx_off_t offs) const {
    if constexpr (sizeof(T) == sizeof(uint8_t)) {
      Write8(val, offs);
    } else if constexpr (sizeof(T) == sizeof(uint16_t)) {
      Write16(val, offs);
    } else if constexpr (sizeof(T) == sizeof(uint32_t)) {
      Write32(val, offs);
    } else if constexpr (sizeof(T) == sizeof(uint64_t)) {
      Write64(val, offs);
    } else {
      static_assert(always_false<T>);
    }
  }

  template <typename T>
  void ModifyBits(T bits, T mask, zx_off_t offs) const {
    T val = Read<T>(offs);
    Write<T>(static_cast<T>((val & ~mask) | (bits & mask)), offs);
  }

  template <typename T>
  void SetBits(T bits, zx_off_t offs) const {
    ModifyBits<T>(bits, bits, offs);
  }

  template <typename T>
  void ClearBits(T bits, zx_off_t offs) const {
    ModifyBits<T>(0, bits, offs);
  }

  template <typename T>
  T GetBits(size_t shift, size_t count, zx_off_t offs) const {
    T mask = static_cast<T>(((static_cast<T>(1) << count) - 1) << shift);
    T val = Read<T>(offs);
    return static_cast<T>((val & mask) >> shift);
  }

  template <typename T>
  T GetBit(size_t shift, zx_off_t offs) const {
    return GetBits<T>(shift, 1, offs);
  }

  template <typename T>
  void ModifyBits(T bits, size_t shift, size_t count, zx_off_t offs) const {
    T mask = static_cast<T>(((static_cast<T>(1) << count) - 1) << shift);
    T val = Read<T>(offs);
    Write<T>(static_cast<T>((val & ~mask) | ((bits << shift) & mask)), offs);
  }

  template <typename T>
  void ModifyBit(bool val, size_t shift, zx_off_t offs) const {
    ModifyBits<T>(val, shift, 1, offs);
  }

  template <typename T>
  void SetBit(size_t shift, zx_off_t offs) const {
    ModifyBit<T>(true, shift, offs);
  }

  template <typename T>
  void ClearBit(size_t shift, zx_off_t offs) const {
    ModifyBit<T>(false, shift, offs);
  }

  uint8_t Read8(zx_off_t offs) const { return ops_->Read8(ctx_, mmio_, offs); }
  uint16_t Read16(zx_off_t offs) const { return ops_->Read16(ctx_, mmio_, offs); }
  uint32_t Read32(zx_off_t offs) const { return ops_->Read32(ctx_, mmio_, offs); }
  uint64_t Read64(zx_off_t offs) const { return ops_->Read64(ctx_, mmio_, offs); }

  // Read `size` bytes from the MmioBuffer into `buffer`. There are no access width guarantees
  // when using this operation and must only be used with devices where arbitrary access widths are
  // supported.
  void ReadBuffer(zx_off_t offs, void* buffer, size_t size) const {
    return ops_->ReadBuffer(ctx_, mmio_, offs, buffer, size);
  }

  void Write8(uint8_t val, zx_off_t offs) const { ops_->Write8(ctx_, mmio_, val, offs); }
  void Write16(uint16_t val, zx_off_t offs) const { ops_->Write16(ctx_, mmio_, val, offs); }
  void Write32(uint32_t val, zx_off_t offs) const { ops_->Write32(ctx_, mmio_, val, offs); }
  void Write64(uint64_t val, zx_off_t offs) const { ops_->Write64(ctx_, mmio_, val, offs); }

  // Write `size` bytes from `buffer` into the MmioBuffer. There are no access width guarantees
  // when using this operation and must only be used with devices where arbitrary access widths are
  // supported.
  void WriteBuffer(zx_off_t offs, const void* buffer, size_t size) const {
    ops_->WriteBuffer(ctx_, mmio_, offs, buffer, size);
  }

 protected:
  mmio_buffer_t mmio_;
  const internal::MmioBufferOps* ops_;
  const void* ctx_;

  template <typename T>
  static constexpr std::false_type always_false{};

 private:
  void transfer(MmioBuffer&& other) {
    mmio_ = other.mmio_;
    ops_ = other.ops_;
    ctx_ = other.ctx_;
    memset(&other.mmio_, 0, sizeof(other.mmio_));
  }
};

}  // namespace fdf

#endif

#endif  // SRC_DEVICES_LIB_MMIO_INCLUDE_LIB_MMIO_MMIO_BUFFER_H_
