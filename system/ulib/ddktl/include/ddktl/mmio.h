// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>
#include <fbl/type_support.h>
#include <hw/arch_ops.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/process.h>

/*
    MmioBlock is used to hold a reference to a block of memory mapped I/O, intended
    to be used in platform device drivers.
*/
namespace ddk {

class MmioBlock {

public:
    friend class Pdev;

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(MmioBlock);
    DISALLOW_NEW;

    MmioBlock() = default;

    MmioBlock(MmioBlock&& other) {
        transfer(fbl::move(other));
    }

    ~MmioBlock() {
        // If we have a valid pointer, unmap on the way out
        if (isMapped()) {
            __UNUSED zx_status_t status;
            status = zx_vmar_unmap(zx_vmar_root_self(), ptr_, len_);
            ZX_DEBUG_ASSERT(status == ZX_OK);
        }
    }
    // Allow assignment from an rvalue
    MmioBlock& operator=(MmioBlock&& other) {
        transfer(fbl::move(other));
        return *this;
    }

    void reset() {
        len_ = 0;
        ptr_ = 0;
    }

    void Info() const {
        zxlogf(INFO, "ptr = %lx\n", ptr_);
        zxlogf(INFO, "len = %lu\n", len_);
    }

    bool isMapped() const {
        return (ptr_ != 0);
    }
    void* get() const {
        return reinterpret_cast<void*>(ptr_);
    }

    /*
        The following methods assume that the MmioBlock instance has been
        successfully initialized by the friend class Pdev.  Method calls against
        an uninitialized instance will result in a crash.

        isMapped() may be used at any time to determine if the MmioBlock instance
        is safe for these operations.
    */
    uint32_t Read32(zx_off_t offs) const {
        return Read<uint32_t>(offs);
    }

    uint32_t ReadMasked32(uint32_t mask, zx_off_t offs) const {
        return ReadMasked<uint32_t>(mask, offs);
    }

    void Write32(uint32_t val, zx_off_t offs) const {
        Write<uint32_t>(val, offs);
    }

    void SetBits32(uint32_t mask, zx_off_t offs) const {
        SetBits<uint32_t>(mask, offs);
    }

    void ClearBits32(uint32_t mask, zx_off_t offs) const {
        ClearBits<uint32_t>(mask, offs);
    }
private:
    void transfer(MmioBlock&& other) {
        len_ = other.len_;
        ptr_ = other.ptr_;
        other.reset();
    }

    MmioBlock(void* ptr, size_t len)
        : ptr_(reinterpret_cast<uintptr_t>(ptr)),
          len_(len) {}

    template <typename T>
    T Read(zx_off_t offs) const {
        ZX_DEBUG_ASSERT(offs + sizeof(T) < len_);
        ZX_DEBUG_ASSERT(ptr_);
        return *reinterpret_cast<volatile T*>(ptr_ + offs);
    }

    template <typename T>
    T ReadMasked(T mask, zx_off_t offs) const {
        return (Read<T>(offs) & mask);
    }

    template <typename T>
    void Write(T val, zx_off_t offs) const {
        ZX_DEBUG_ASSERT(offs + sizeof(T) < len_);
        ZX_DEBUG_ASSERT(ptr_);
        *reinterpret_cast<volatile T*>(ptr_ + offs) = val;
        hw_mb();
    }

    template <typename T>
    void SetBits(T mask, zx_off_t offs) const {
        T val = Read<T>(offs);
        Write<T>(val | mask, offs);
    }

    template <typename T>
    void ClearBits(T mask, zx_off_t offs) const {
        T val = Read<T>(offs);
        Write<T>(val & ~mask, offs);
    }

    uintptr_t ptr_ = 0;
    size_t len_ = 0;
};

} //namespace ddk