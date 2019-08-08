/*
 * Copyright (c) 2019 The Fuchsia Authors.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "sim.h"

#include <zircon/status.h>

#include <memory>

#include "bus.h"
#include "chip.h"
#include "common.h"
#include "debug.h"
#include "device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"

#define BUS_OP(dev) dev->bus->bus_priv.sim->sim_fw
static const struct brcmf_bus_ops brcmf_sim_bus_ops = {
    .get_bus_type = []() { return BRCMF_BUS_TYPE_SIM; },
    .preinit = [](struct brcmf_device* dev) { return BUS_OP(dev)->BusPreinit(); },
    .stop = [](struct brcmf_device* dev) { return BUS_OP(dev)->BusStop(); },
    .txdata = [](struct brcmf_device* dev,
                 struct brcmf_netbuf* netbuf) { return BUS_OP(dev)->BusTxData(netbuf); },
    .txctl = [](struct brcmf_device* dev, unsigned char* msg,
                uint len) { return BUS_OP(dev)->BusTxCtl(msg, len); },
    .rxctl = [](struct brcmf_device* dev, unsigned char* msg, uint len,
                int* rxlen_out) { return BUS_OP(dev)->BusRxCtl(msg, len, rxlen_out); },
    .gettxq = [](struct brcmf_device* dev) { return BUS_OP(dev)->BusGetTxQueue(); },
    .wowl_config = [](struct brcmf_device* dev,
                      bool enabled) { return BUS_OP(dev)->BusWowlConfig(enabled); },
    .get_ramsize = [](struct brcmf_device* dev) { return BUS_OP(dev)->BusGetRamsize(); },
    .get_memdump = [](struct brcmf_device* dev, void* data,
                      size_t len) { return BUS_OP(dev)->BusGetMemdump(data, len); },
    .get_fwname =
        [](struct brcmf_device* dev, uint chip, uint chiprev, unsigned char* fw_name) {
          return BUS_OP(dev)->BusGetFwName(chip, chiprev, fw_name);
        },
    .get_bootloader_macaddr =
        [](struct brcmf_device* dev, uint8_t* mac_addr) {
          return BUS_OP(dev)->BusGetBootloaderMacAddr(mac_addr);
        },
    .device_add = wlan_sim_device_add};
#undef BUS_OP

// Get device-specific information
zx_status_t brcmf_sim_probe(struct brcmf_bus* bus) {
  uint32_t chip, chiprev;

  bus->bus_priv.sim->sim_fw->GetChipInfo(&chip, &chiprev);
  bus->chip = chip;
  bus->chiprev = chiprev;

  bus->bus_priv.sim->settings = brcmf_get_module_param(bus->dev, BRCMF_BUS_TYPE_SIM, chip, chiprev);
  if (bus->bus_priv.sim->settings == nullptr) {
    BRCMF_ERR("Failed to get device parameters\n");
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

// Allocate necessary memory and initialize simulator-specific structures
zx_status_t brcmf_sim_register(struct brcmf_device* device) {
  zx_status_t status = ZX_OK;
  auto simdev = std::make_unique<brcmf_simdev>();
  auto bus_if = std::make_unique<brcmf_bus>();

  // Initialize inter-structure pointers
  simdev->sim_fw = std::make_unique<SimFirmware>();
  bus_if->bus_priv.sim = simdev.get();
  bus_if->dev = device;

  BRCMF_DBG(SIM, "Registering simulator target\n");
  status = brcmf_sim_probe(bus_if.get());
  if (status != ZX_OK) {
    BRCMF_ERR("sim_probe failed: %s\n", zx_status_get_string(status));
    return status;
  }

  bus_if->ops = &brcmf_sim_bus_ops;
  device->bus = std::move(bus_if);
  status = brcmf_attach(device, simdev->settings);
  if (status != ZX_OK) {
    BRCMF_ERR("brcmf_attach failed\n");
    return status;
  }

  // Here is where we would likely simulate loading the firmware into the target. For now,
  // we don't try.

  status = brcmf_bus_started(device);
  if (status != ZX_OK) {
    BRCMF_ERR("Failed to start (simulated) bus\n");
    return status;
  }

  simdev.release();
  return ZX_OK;
}

void brcmf_sim_exit(struct brcmf_device* device) {
  delete device->bus->bus_priv.sim;
  device->bus->bus_priv.sim = nullptr;

  device->bus.reset();
}
