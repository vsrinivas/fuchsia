// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/bti.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/process.h>

#include <optional>
#include <utility>

#include <ddk/debug.h>
#include <ddk/mmio-buffer.h>
#include <fake-mmio-reg/fake-mmio-reg.h>
#include <fbl/macros.h>
#include <hw/arch_ops.h>

namespace ddk {

// See system/dev/lib/mmio/include/lib/mmio/mmio.h.

class MmioPinnedBuffer {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(MmioPinnedBuffer);

  explicit MmioPinnedBuffer(mmio_pinned_buffer_t pinned) : pinned_(pinned) {}

  ~MmioPinnedBuffer() { mmio_buffer_unpin(&pinned_); }

  MmioPinnedBuffer(MmioPinnedBuffer&& other) { transfer(std::move(other)); }

  MmioPinnedBuffer& operator=(MmioPinnedBuffer&& other) {
    transfer(std::move(other));
    return *this;
  }

  void reset() { memset(&pinned_, 0, sizeof(pinned_)); }

  zx_paddr_t get_paddr() const { return pinned_.paddr; }

 private:
  void transfer(MmioPinnedBuffer&& other) {
    pinned_ = other.pinned_;
    other.reset();
  }
  mmio_pinned_buffer_t pinned_;
};

// Forward declaration.
class MmioView;

// MmioBuffer is wrapper around mmio_block_t.
class MmioBuffer {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(MmioBuffer);

  explicit MmioBuffer(mmio_buffer_t mmio)
      : mmio_(mmio), ptr_(reinterpret_cast<uintptr_t>(mmio.vaddr)) {}

  virtual ~MmioBuffer() {}

  MmioBuffer(MmioBuffer&& other) { transfer(std::move(other)); }

  MmioBuffer& operator=(MmioBuffer&& other) {
    transfer(std::move(other));
    return *this;
  }

  // Create() is not implemented.
  static zx_status_t Create(zx_off_t offset, size_t size, zx::vmo vmo, uint32_t cache_policy,
                            std::optional<MmioBuffer>* mmio_buffer) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Pin() returns an invalid paddr.

  void reset() {}

  void Info() const {
    zxlogf(INFO, "vaddr = %p\n", mmio_.vaddr);
    zxlogf(INFO, "size = %lu\n", mmio_.size);
  }

  void* get() const { return mmio_.vaddr; }
  size_t get_size() const { return mmio_.size; }
  zx::unowned_vmo get_vmo() const { return zx::unowned_vmo(mmio_.vmo); }

  MmioView View(zx_off_t off) const;
  MmioView View(zx_off_t off, size_t size) const;

  uint32_t Read32(zx_off_t offs) const { return Read<uint32_t>(offs); }

  uint32_t ReadMasked32(uint32_t mask, zx_off_t offs) const {
    return ReadMasked<uint32_t>(mask, offs);
  }

  void Write32(uint32_t val, zx_off_t offs) const { Write<uint32_t>(val, offs); }

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
    // MockMmioRegRegion.GetMmioBuffer() returns an mmio_buffer_t with an offset of 0. This
    // mmio_buffer_t could be used to create a ddk::MmioView with a non-zero offset, so the
    // original location of the MockMmioRegRegion needs to be calculated.
    uint8_t* ptr = reinterpret_cast<uint8_t*>(mmio_.vaddr) - mmio_.offset;
    ddk_fake::FakeMmioRegRegion* mock_regs = reinterpret_cast<ddk_fake::FakeMmioRegRegion*>(ptr);
    ZX_ASSERT(mock_regs != nullptr);
    return static_cast<T>((*mock_regs)[offs + mmio_.offset].Read());
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
    uint8_t* ptr = reinterpret_cast<uint8_t*>(mmio_.vaddr) - mmio_.offset;
    ddk_fake::FakeMmioRegRegion* mock_regs = reinterpret_cast<ddk_fake::FakeMmioRegRegion*>(ptr);
    ZX_ASSERT(mock_regs != nullptr);
    (*mock_regs)[offs + mmio_.offset].Write(val);
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

  zx_status_t Pin(const zx::bti& bti, std::optional<MmioPinnedBuffer>* pinned_buffer) {
    mmio_pinned_buffer_t pinned;
    pinned.mmio = &mmio_;
    pinned.pmt = ZX_HANDLE_INVALID;
    pinned.paddr = 0;
    *pinned_buffer = MmioPinnedBuffer(pinned);
    return ZX_OK;
  }

 protected:
  mmio_buffer_t mmio_;

 private:
  void transfer(MmioBuffer&& other) {
    mmio_ = other.mmio_;
    ptr_ = other.ptr_;
    other.reset();
  }

  uintptr_t ptr_;
};

class MmioView : public MmioBuffer {
 public:
  MmioView(const mmio_buffer_t& mmio, zx_off_t offset)
      : MmioBuffer(mmio_buffer_t{
            .vaddr = static_cast<uint8_t*>(mmio.vaddr) + offset,
            .offset = mmio.offset + offset,
            .size = mmio.size - offset,
            .vmo = mmio.vmo,
        }) {
    ZX_ASSERT(offset < mmio.size);
  }

  MmioView(const mmio_buffer_t& mmio, zx_off_t offset, size_t size)
      : MmioBuffer(mmio_buffer_t{
            .vaddr = static_cast<uint8_t*>(mmio.vaddr) + offset,
            .offset = mmio.offset + offset,
            .size = size,
            .vmo = mmio.vmo,
        }) {
    ZX_ASSERT(size + offset <= mmio.size);
  }

  MmioView(const MmioView& mmio) : MmioBuffer(mmio.mmio_) {}

  virtual ~MmioView() override {
    // Prevent unmap operation from occurring.
    mmio_.vmo = ZX_HANDLE_INVALID;
  }
};

// These can't be defined inside the class because they need MmioView
// to be completely defined first.

inline MmioView MmioBuffer::View(zx_off_t off) const { return MmioView(mmio_, off); }

inline MmioView MmioBuffer::View(zx_off_t off, size_t size) const {
  return MmioView(mmio_, off, size);
}

}  // namespace ddk
