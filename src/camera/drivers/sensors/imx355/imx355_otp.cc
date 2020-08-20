// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include <lib/fzl/vmo-mapper.h>

#include <climits>

#include <ddk/debug.h>

#include "src/camera/drivers/sensors/imx355/imx355.h"
#include "src/camera/drivers/sensors/imx355/imx355_otp_config.h"

namespace camera {

fit::result<zx::vmo, zx_status_t> Imx355Device::OtpRead() {
  std::lock_guard guard(lock_);

  fzl::VmoMapper mapper;
  zx::vmo vmo;
  zx_status_t status =
      mapper.CreateAndMap(OTP_TOTAL_SIZE, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to create and map VMO", __func__);
    return fit::error(status);
  }

  // TODO(jsasinowski): Read the EEPROM contents

  mapper.Unmap();
  return fit::ok(std::move(vmo));
}

bool Imx355Device::OtpValidate(const zx::vmo& vmo) { return true; }

}  // namespace camera
