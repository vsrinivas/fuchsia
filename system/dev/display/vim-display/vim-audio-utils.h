// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/platform-device-lib.h>
#include <ddktl/mmio.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

namespace audio {
namespace vim2 {

class Registers : public fbl::RefCounted<Registers>,
                  public ddk::MmioBuffer {
  public:
    static fbl::RefPtr<Registers> Create(const pdev_protocol_t* pdev,
                                         uint32_t which_mmio,
                                         zx_status_t* out_res);
    DISALLOW_COPY_ASSIGN_AND_MOVE(Registers);

  private:
    friend class fbl::RefPtr<Registers>;

    Registers(mmio_buffer_t& mmio) : ddk::MmioBuffer(mmio) {}
    ~Registers() = default;
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
