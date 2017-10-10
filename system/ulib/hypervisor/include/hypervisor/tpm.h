// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hypervisor/address.h>
#include <hypervisor/guest.h>
#include <hypervisor/io.h>
#include <zircon/types.h>

// Stub TPM implementation.
class TpmHandler : public IoHandler {
public:
    zx_status_t Init(Guest* guest) {
        return guest->CreateMapping(TrapType::MMIO_SYNC, TPM_PHYS_BASE, TPM_SIZE, 0, this);
    }

    zx_status_t Read(uint64_t addr, IoValue* value) override { return ZX_OK; }
    zx_status_t Write(uint64_t addr, const IoValue& value) override { return ZX_OK; }
};
