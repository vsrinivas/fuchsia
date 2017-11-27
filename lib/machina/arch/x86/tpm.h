// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ARCH_X86_TPM_H_
#define GARNET_LIB_MACHINA_ARCH_X86_TPM_H_

#include <hypervisor/address.h>
#include <hypervisor/guest.h>
#include <hypervisor/io.h>
#include <zircon/types.h>

// Stub TPM implementation.
class Tpm : public IoHandler {
public:
    zx_status_t Init(Guest* guest) {
        return guest->CreateMapping(TrapType::MMIO_SYNC, TPM_PHYS_BASE, TPM_SIZE, 0, this);
    }

    zx_status_t Read(uint64_t addr, IoValue* value) const override { return ZX_OK; }
    zx_status_t Write(uint64_t addr, const IoValue& value) override { return ZX_OK; }
};

#endif  // GARNET_LIB_MACHINA_ARCH_X86_TPM_H_
