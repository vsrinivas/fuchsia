// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-meson/g12b-clk.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"
#include "src/devices/board/drivers/sherlock/sherlock-hevc-enc-bind.h"

namespace sherlock {

static pbus_mmio_t sherlock_hevc_enc_mmios[] = {
    {
        .base = T931_CBUS_BASE,
        .length = T931_CBUS_LENGTH,
    },
    {
        .base = T931_DOS_BASE,
        .length = T931_DOS_LENGTH,
    },
    {
        .base = T931_AOBUS_BASE,
        .length = T931_AOBUS_LENGTH,
    },
    {
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
    {
        .base = T931_WAVE420L_BASE,
        .length = T931_WAVE420L_LENGTH,
    },
};

constexpr pbus_bti_t sherlock_hevc_enc_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_HEVC_ENC,
    },
};

constexpr pbus_irq_t sherlock_hevc_enc_irqs[] = {
    {
        .irq = T931_WAVE420L_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static pbus_dev_t hevc_enc_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-hevc-enc";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_HEVC_ENC;
  dev.mmio_list = sherlock_hevc_enc_mmios;
  dev.mmio_count = countof(sherlock_hevc_enc_mmios);
  dev.bti_list = sherlock_hevc_enc_btis;
  dev.bti_count = countof(sherlock_hevc_enc_btis);
  dev.irq_list = sherlock_hevc_enc_irqs;
  dev.irq_count = countof(sherlock_hevc_enc_irqs);
  return dev;
}();

zx_status_t Sherlock::HevcEncInit() {
  zx_status_t status =
      pbus_.AddComposite(&hevc_enc_dev, reinterpret_cast<uint64_t>(aml_hevc_enc_fragments),
                         countof(aml_hevc_enc_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "Sherlock::HevcEncInit: AddComposite() failed: %d", status);
    return status;
  }
  return ZX_OK;
}

}  // namespace sherlock
