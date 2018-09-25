// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string.h>

#include <ddk/debug.h>
#include <ddk/mmio-buffer.h>
#include <fbl/macros.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>
#include <hw/arch_ops.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/process.h>

namespace ddk {

class MmioPinnedBuffer;

// MmioBuffer is wrapper around mmio_block_t.
class MmioBuffer {

public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(MmioBuffer);

    MmioBuffer(mmio_buffer_t& mmio)
        : mmio_(mmio), ptr_(reinterpret_cast<uintptr_t>(mmio.vaddr)) {
        ZX_ASSERT(mmio_.vaddr != nullptr);
    }

    ~MmioBuffer() {
        mmio_buffer_release(&mmio_);
    }

    MmioBuffer(MmioBuffer&& other) {
        transfer(fbl::move(other));
    }

    MmioBuffer& operator=(MmioBuffer&& other) {
        transfer(fbl::move(other));
        return *this;
    }

    static zx_status_t Create(zx_off_t offset, size_t size, zx::vmo vmo, uint32_t cache_policy,
                              fbl::unique_ptr<MmioBuffer>* mmio_buffer) {
        mmio_buffer_t mmio;
        zx_status_t status = mmio_buffer_init(&mmio, offset, size, vmo.release(), cache_policy);
        if (status == ZX_OK) {
            *mmio_buffer = fbl::make_unique<MmioBuffer>(mmio);
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

    zx_status_t Pin(const zx::bti& bti, fbl::unique_ptr<MmioPinnedBuffer>* pinned_buffer) {
        mmio_pinned_buffer_t pinned;
        zx_status_t status = mmio_buffer_pin(&mmio_, bti.get(), &pinned);
        if (status == ZX_OK) {
            *pinned_buffer = fbl::make_unique<MmioPinnedBuffer>(pinned);
        }
        return status;
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

    void SetBits32(uint32_t bits, zx_off_t offs) const {
        SetBits<uint32_t>(bits, offs);
    }

    void ClearBits32(uint32_t bits, zx_off_t offs) const {
        ClearBits<uint32_t>(bits, offs);
    }

    template <typename T>
    T Read(zx_off_t offs) const {
        ZX_DEBUG_ASSERT(offs + sizeof(T) < mmio_.size);
        ZX_DEBUG_ASSERT(ptr_);
        return *reinterpret_cast<volatile T*>(ptr_ + offs);
    }

    template <typename T>
    T ReadMasked(T mask, zx_off_t offs) const {
        return (Read<T>(offs) & mask);
    }

    template <typename T>
    void Write(T val, zx_off_t offs) const {
        ZX_DEBUG_ASSERT(offs + sizeof(T) < mmio_.size);
        ZX_DEBUG_ASSERT(ptr_);
        *reinterpret_cast<volatile T*>(ptr_ + offs) = val;
        hw_mb();
    }

    template <typename T>
    void ModifyBits(T bits, T mask, zx_off_t offs) const {
        T val = Read<T>(offs);
        Write<T>((val & ~mask) | (bits & mask), offs);
    }

    template <typename T>
    void SetBits(T bits, zx_off_t offs) const {
        ModifyBits<T>(bits, bits, offs);
    }

    template <typename T>
    void ClearBits(T bits, zx_off_t offs) const {
        ModifyBits<T>(0, bits, offs);
    }

private:
    void transfer(MmioBuffer&& other) {
        mmio_ = other.mmio_;
        ptr_ = other.ptr_;
        other.reset();
    }

    mmio_buffer_t mmio_;
    uintptr_t ptr_;
};

// MmioPinnedBuffer is wrapper around mmio_pinned_buffer_t.
class MmioPinnedBuffer {

public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(MmioPinnedBuffer);

    MmioPinnedBuffer(mmio_pinned_buffer_t pinned)
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

} //namespace ddk
