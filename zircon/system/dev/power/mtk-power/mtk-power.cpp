// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/pdev.h>

#include "mtk-power.h"

namespace power {

zx_status_t MtkPower::PowerImplDisablePowerDomain(uint32_t index) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkPower::PowerImplEnablePowerDomain(uint32_t index) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkPower::PowerImplGetPowerDomainStatus(uint32_t index,
                                                    power_domain_status_t* out_status) {
    return ZX_ERR_NOT_SUPPORTED;
}

void DdkRelease() {
  // Nothing for now
}
void DdkUnbind() {
  // Nothing for now
}

}; //namespace power
