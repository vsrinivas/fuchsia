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

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"

#include <zircon/status.h>

#include <memory>

#include <wifi/wifi-config.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bus.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chip.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"

#define BUS_OP(bus) bus->bus_priv.sim->sim_fw
static const struct brcmf_bus_ops brcmf_sim_bus_ops = {
    .get_bus_type = []() { return BRCMF_BUS_TYPE_SIM; },
    .preinit = [](brcmf_bus* bus) { return BUS_OP(bus)->BusPreinit(); },
    .stop = [](brcmf_bus* bus) { return BUS_OP(bus)->BusStop(); },
    .txdata = [](brcmf_bus* bus, brcmf_netbuf* netbuf) { return BUS_OP(bus)->BusTxData(netbuf); },
    .txctl = [](brcmf_bus* bus, unsigned char* msg,
                uint len) { return BUS_OP(bus)->BusTxCtl(msg, len); },
    .rxctl = [](brcmf_bus* bus, unsigned char* msg, uint len,
                int* rxlen_out) { return BUS_OP(bus)->BusRxCtl(msg, len, rxlen_out); },
    .gettxq = [](brcmf_bus* bus) { return BUS_OP(bus)->BusGetTxQueue(); },
    .wowl_config = [](brcmf_bus* bus, bool enabled) { return BUS_OP(bus)->BusWowlConfig(enabled); },
    .get_ramsize = [](brcmf_bus* bus) { return BUS_OP(bus)->BusGetRamsize(); },
    .get_memdump = [](brcmf_bus* bus, void* data,
                      size_t len) { return BUS_OP(bus)->BusGetMemdump(data, len); },
    .get_fwname =
        [](brcmf_bus* bus, uint chip, uint chiprev, unsigned char* fw_name, size_t* fw_name_size) {
          return BUS_OP(bus)->BusGetFwName(chip, chiprev, fw_name, fw_name_size);
        },
    .get_bootloader_macaddr =
        [](brcmf_bus* bus, uint8_t* mac_addr) {
          return BUS_OP(bus)->BusGetBootloaderMacAddr(mac_addr);
        },
    .get_wifi_metadata =
        [](zx_device_t* zx_dev, void* data, size_t exp_size, size_t* actual) {
          wifi_config_t wifi_config = {
              .oob_irq_mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
              .iovar_table =
                  {
                      {IOVAR_LIST_END_TYPE, {{0}}, 0},
                  },
              .cc_table =
                  {
                      {"US", 842},
                      {"WW", 999},
                      {"", 0},
                  },
          };
          memcpy(data, &wifi_config, sizeof(wifi_config));
          *actual = sizeof(wifi_config);
          return ZX_OK;
        },
};
#undef BUS_OP

// Get device-specific information
zx_status_t brcmf_sim_probe(struct brcmf_bus* bus) {
  uint32_t chip, chiprev;

  bus->bus_priv.sim->sim_fw->GetChipInfo(&chip, &chiprev);
  bus->chip = chip;
  bus->chiprev = chiprev;

  brcmf_get_module_param(BRCMF_BUS_TYPE_SIM, chip, chiprev, bus->bus_priv.sim->settings.get());
  return ZX_OK;
}

// Allocate necessary memory and initialize simulator-specific structures
zx_status_t brcmf_sim_register(brcmf_pub* drvr, std::unique_ptr<brcmf_bus>* out_bus,
                               ::wlan::simulation::FakeDevMgr* dev_mgr,
                               ::wlan::simulation::Environment* env) {
  zx_status_t status = ZX_OK;
  auto simdev = std::make_unique<brcmf_simdev>();
  auto bus_if = std::make_unique<brcmf_bus>();

  // Initialize inter-structure pointers
  simdev->drvr = drvr;
  simdev->sim_fw = std::make_unique<::wlan::brcmfmac::SimFirmware>(simdev.get(), env);
  simdev->dev_mgr = dev_mgr;
  simdev->settings = std::make_unique<brcmf_mp_device>();
  bus_if->bus_priv.sim = simdev.get();

  BRCMF_DBG(SIM, "Registering simulator target\n");
  status = brcmf_sim_probe(bus_if.get());
  if (status != ZX_OK) {
    BRCMF_ERR("sim_probe failed: %s\n", zx_status_get_string(status));
    return status;
  }

  bus_if->ops = &brcmf_sim_bus_ops;
  drvr->bus_if = bus_if.get();
  drvr->settings = simdev->settings.get();
  status = brcmf_attach(drvr);
  if (status != ZX_OK) {
    BRCMF_ERR("brcmf_attach failed\n");
    return status;
  }

  status = brcmf_proto_bcdc_attach(drvr);
  if (status != ZX_OK) {
    BRCMF_ERR("brcmf_proto_bcdc_attach failed: %s\n", zx_status_get_string(status));
    return status;
  }

  // Here is where we would likely simulate loading the firmware into the target. For now,
  // we don't try.

  status = brcmf_bus_started(drvr);
  if (status != ZX_OK) {
    BRCMF_ERR("Failed to start (simulated) bus\n");
    return status;
  }

  simdev.release();
  *out_bus = std::move(bus_if);
  return ZX_OK;
}

// Handle a simulator event: allocate a netbuf, copy the data, and pass it to the driver,
// which takes ownership of the memory. Copying the data isn't ideal, but performance isn't
// an issue in the simulator and it's cleaner to draw a line between memory owned by the
// simulator and memory owned by the driver, with this file being the only thing that straddles
// that boundary.
void brcmf_sim_rx_event(brcmf_simdev* simdev, std::unique_ptr<std::vector<uint8_t>> buffer) {
  uint32_t packet_len = buffer->size();
  if (packet_len < ETH_HLEN) {
    BRCMF_ERR("Malformed packet\n");
    return;
  }
  brcmf_netbuf* netbuf = brcmf_netbuf_allocate(packet_len);
  memcpy(netbuf->data, buffer->data(), packet_len);
  brcmf_netbuf_set_length_to(netbuf, packet_len);
  brcmf_rx_event(simdev->drvr, netbuf);
}

void brcmf_sim_exit(brcmf_bus* bus) {
  delete bus->bus_priv.sim;
  bus->bus_priv.sim = nullptr;
}
