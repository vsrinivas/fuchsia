// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <zircon/syscalls/smc.h>

#include <soc/aml-a5/a5-hw.h>

#include "av400.h"
#include "src/devices/board/drivers/av400/av400-dsp-bind.h"

namespace av400 {

static constexpr pbus_mmio_t dsp_mmios[] = {
    {
        .base = A5_DSPA_BASE,
        .length = A5_DSPA_BASE_LENGTH,
    },
    {
        .base = A5_DSP_SRAM_BASE,
        .length = A5_DSP_SRAM_BASE_LENGTH,
    },
};

static constexpr pbus_smc_t dsp_smcs[] = {
    {
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_BASE,
        .count = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_LENGTH,
        .exclusive = false,
    },
};

static pbus_dev_t dsp_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "dsp";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_A5;
  dev.did = PDEV_DID_AMLOGIC_DSP;
  dev.mmio_list = dsp_mmios;
  dev.mmio_count = std::size(dsp_mmios);
  dev.smc_list = dsp_smcs;
  dev.smc_count = std::size(dsp_smcs);
  return dev;
}();

zx_status_t Av400::DspInit() {
  zx_status_t status = pbus_.AddComposite(&dsp_dev, reinterpret_cast<uint64_t>(av400_dsp_fragments),
                                          std::size(av400_dsp_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "DeviceAdd failed %s", zx_status_get_string(status));
  }
  return status;
}

}  // namespace av400
