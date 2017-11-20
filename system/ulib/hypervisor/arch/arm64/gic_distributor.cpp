// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/gic_distributor.h>

zx_status_t GicDistributor::Init(Guest* guest) {
    return ZX_OK;
}

zx_status_t GicDistributor::Read(uint64_t addr, IoValue* value) const {
    return ZX_OK;
}

zx_status_t GicDistributor::Write(uint64_t addr, const IoValue& value) {
    return ZX_OK;
}

zx_status_t GicDistributor::Interrupt(uint32_t global_irq) const {
    return ZX_OK;
}
