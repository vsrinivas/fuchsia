// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/syscalls/smc.h>

#include <soc/aml-meson/g12b-clk.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"
#include "src/devices/board/drivers/sherlock/sherlock-video-bind.h"

namespace sherlock {

static pbus_mmio_t sherlock_video_mmios[] = {
    {
        .base = T931_CBUS_BASE,
        .length = T931_CBUS_LENGTH,
    },
    {
        .base = T931_DOS_BASE,
        .length = T931_DOS_LENGTH,
    },
    {
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
    {
        .base = T931_AOBUS_BASE,
        .length = T931_AOBUS_LENGTH,
    },
    {
        .base = T931_DMC_BASE,
        .length = T931_DMC_LENGTH,
    },
};

constexpr pbus_bti_t sherlock_video_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_VIDEO,
    },
};

constexpr pbus_irq_t sherlock_video_irqs[] = {
    {
        .irq = T931_DEMUX_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = T931_PARSER_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = T931_DOS_MBOX_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = T931_DOS_MBOX_1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr pbus_smc_t sherlock_video_smcs[] = {
    {
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_TRUSTED_OS_BASE,
        .count = 1,
        .exclusive = false,
    },
};

static pbus_dev_t video_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-video";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_VIDEO;
  dev.mmio_list = sherlock_video_mmios;
  dev.mmio_count = std::size(sherlock_video_mmios);
  dev.bti_list = sherlock_video_btis;
  dev.bti_count = std::size(sherlock_video_btis);
  dev.irq_list = sherlock_video_irqs;
  dev.irq_count = std::size(sherlock_video_irqs);
  dev.smc_list = sherlock_video_smcs;
  dev.smc_count = std::size(sherlock_video_smcs);
  return dev;
}();

zx_status_t Sherlock::VideoInit() {
  zx_status_t status =
      pbus_.AddComposite(&video_dev, reinterpret_cast<uint64_t>(aml_video_fragments),
                         std::size(aml_video_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "Sherlock::VideoInit: AddComposite() failed for video: %d", status);
    return status;
  }
  return ZX_OK;
}

}  // namespace sherlock
