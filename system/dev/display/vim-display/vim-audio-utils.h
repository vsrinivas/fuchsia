// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform-device.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

namespace audio {
namespace vim2 {

class Registers : public fbl::RefCounted<Registers> {
  public:
    static fbl::RefPtr<Registers> Create(const platform_device_protocol_t* pdev,
                                         uint32_t which_mmio,
                                         zx_status_t* out_res);
    DISALLOW_COPY_ASSIGN_AND_MOVE(Registers);

    bool valid() const { return base_ != nullptr; }

    volatile uint32_t& operator[](uint32_t r) const { return base_[r]; }
    void SetBits(uint32_t r, uint32_t bits) const { ModBits(r, bits, bits); }
    void ClrBits(uint32_t r, uint32_t bits) const { ModBits(r, bits, 0u); }
    void ModBits(uint32_t r, uint32_t mask, uint32_t bits) const {
        (*this)[r] = ((*this)[r] & ~mask) | (bits & mask);
    }

  private:
    friend class fbl::RefPtr<Registers>;

    Registers() = default;
    ~Registers();

    zx_status_t Map(const platform_device_protocol_t* pdev, uint32_t which_mmio);

    io_buffer_t buf_ = {};
    volatile uint32_t* base_ = nullptr;
};

class RefCountedVmo : public fbl::RefCounted<RefCountedVmo>{
public:
    static fbl::RefPtr<RefCountedVmo> Create(zx::vmo vmo);
    DISALLOW_COPY_ASSIGN_AND_MOVE(RefCountedVmo);

    const zx::vmo& vmo() { return vmo_; }

private:
    friend class fbl::RefPtr<RefCountedVmo>;

    explicit RefCountedVmo(zx::vmo vmo) : vmo_(fbl::move(vmo)) { }
    ~RefCountedVmo() = default;

    const zx::vmo vmo_;
};

}  // namespace vim2
}  // namespace audio
