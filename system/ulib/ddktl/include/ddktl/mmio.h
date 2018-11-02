// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string.h>

#include <ddk/debug.h>
#include <ddk/mmio-buffer.h>
#include <fbl/macros.h>
#include <fbl/optional.h>
#include <fbl/type_support.h>
#include <hw/arch_ops.h>
#include <lib/zx/bti.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/process.h>

namespace ddk {

// MmioPinnedBuffer is wrapper around mmio_pinned_buffer_t.
class MmioPinnedBuffer {

public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(MmioPinnedBuffer);

    explicit MmioPinnedBuffer(mmio_pinned_buffer_t pinned)
        : pinned_(pinned) {
        ZX_ASSERT(pinned_.paddr != 0);
    }

    ~MmioPinnedBuffer() {
        mmio_buffer_unpin(&pinned_);
    }

    MmioPinnedBuffer(MmioPinnedBuffer&& other) {
        transfer(fbl::move(other));
    }

    MmioPinnedBuffer& operator=(MmioPinnedBuffer&& other) {
        transfer(fbl::move(other));
        return *this;
    }

    void reset() {
        memset(&pinned_, 0, sizeof(pinned_));
    }

    zx_paddr_t get_paddr() const {
        return pinned_.paddr;
    }

private:
    void transfer(MmioPinnedBuffer&& other) {
        pinned_ = other.pinned_;
        other.reset();
    }
    mmio_pinned_buffer_t pinned_;
};

// MmioBase is wrapper around mmio_block_t.
// Use MmioBuffer (defined below) instead of MmioBase.
template <typename ViewType>
class MmioBase {

public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(MmioBase);

    explicit MmioBase(mmio_buffer_t mmio)
        : mmio_(mmio), ptr_(reinterpret_cast<uintptr_t>(mmio.vaddr)) {
        ZX_ASSERT(mmio_.vaddr != nullptr);
    }

    virtual ~MmioBase() {
        mmio_buffer_release(&mmio_);
    }

    MmioBase(MmioBase&& other) {
        transfer(fbl::move(other));
    }

    MmioBase& operator=(MmioBase&& other) {
        transfer(fbl::move(other));
        return *this;
    }

    static zx_status_t Create(zx_off_t offset, size_t size, zx::vmo vmo, uint32_t cache_policy,
                              fbl::optional<MmioBase>* mmio_buffer) {
        mmio_buffer_t mmio;
        zx_status_t status = mmio_buffer_init(&mmio, offset, size, vmo.release(), cache_policy);
        if (status == ZX_OK) {
            *mmio_buffer = MmioBase(mmio);
        }
        return status;
    }

    static zx_status_t Create(zx_paddr_t base, size_t size, const zx::resource& resource,
                              uint32_t cache_policy, fbl::optional<MmioBase>* mmio_buffer) {
        mmio_buffer_t mmio;
        zx_status_t status = mmio_buffer_init_physical(&mmio, base, size, resource.get(),
                                                       cache_policy);
        if (status == ZX_OK) {
            *mmio_buffer = MmioBase(mmio);
        }
        return status;
    }

    void reset() {
        memset(&mmio_, 0, sizeof(mmio_));
    }

    void Info() const {
        zxlogf(INFO, "vaddr = %p\n", mmio_.vaddr);
        zxlogf(INFO, "size = %lu\n", mmio_.size);
    }

    void* get() const {
        return mmio_.vaddr;
    }
    size_t get_size() const {
        return mmio_.size;
    }
    zx::unowned_vmo get_vmo() const {
        return zx::unowned_vmo(mmio_.vmo);
    }

    zx_status_t Pin(const zx::bti& bti, fbl::optional<MmioPinnedBuffer>* pinned_buffer) {
        mmio_pinned_buffer_t pinned;
        zx_status_t status = mmio_buffer_pin(&mmio_, bti.get(), &pinned);
        if (status == ZX_OK) {
            *pinned_buffer = MmioPinnedBuffer(pinned);
        }
        return status;
    }

    // Provides a slice view into the mmio.
    // The returned slice object must not outlive this object.
    ViewType View(zx_off_t off) const {
        return ViewType(mmio_, off);
    }
    ViewType View(zx_off_t off, size_t size) const {
        return ViewType(mmio_, off, size);
    }

    uint32_t Read32(zx_off_t offs) const {
        return Read<uint32_t>(offs);
    }

    uint32_t ReadMasked32(uint32_t mask, zx_off_t offs) const {
        return ReadMasked<uint32_t>(mask, offs);
    }

    void Write32(uint32_t val, zx_off_t offs) const {
        Write<uint32_t>(val, offs);
    }

    void ModifyBits32(uint32_t bits, uint32_t mask, zx_off_t offs) const {
        ModifyBits<uint32_t>(bits, mask, offs);
    }

    void ModifyBits32(uint32_t val, uint32_t start, uint32_t width, zx_off_t offs) const {
        ModifyBits<uint32_t>(val, start, width, offs);
    }

    void SetBits32(uint32_t bits, zx_off_t offs) const {
        SetBits<uint32_t>(bits, offs);
    }

    void ClearBits32(uint32_t bits, zx_off_t offs) const {
        ClearBits<uint32_t>(bits, offs);
    }

    template <typename T>
    T Read(zx_off_t offs) const {
        ZX_DEBUG_ASSERT(offs + sizeof(T) <= mmio_.size);
        ZX_DEBUG_ASSERT(ptr_);
        return *reinterpret_cast<volatile T*>(ptr_ + offs);
    }

    template <typename T>
    T ReadMasked(T mask, zx_off_t offs) const {
        return (Read<T>(offs) & mask);
    }

    template <typename T>
    void Write(T val, zx_off_t offs) const {
        ZX_DEBUG_ASSERT(offs + sizeof(T) <= mmio_.size);
        ZX_DEBUG_ASSERT(ptr_);
        *reinterpret_cast<volatile T*>(ptr_ + offs) = val;
        hw_mb();
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

protected:
    mmio_buffer_t mmio_;

private:
    void transfer(MmioBase&& other) {
        mmio_ = other.mmio_;
        ptr_ = other.ptr_;
        other.reset();
    }

    uintptr_t ptr_;
};

class MmioView;
typedef MmioBase<MmioView> MmioBuffer;

// A sliced view that of an mmio which does not unmap on close. Must outlive
// mmio buffer it is created from.
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
              .size = mmio.size,
              .vmo = mmio.vmo,
          }) {
        ZX_ASSERT(size + offset <= mmio.size);
    }

    virtual ~MmioView() override {
        // Prevent unmap operation from occurring.
        mmio_.vmo = ZX_HANDLE_INVALID;
    }
};

} //namespace ddk
