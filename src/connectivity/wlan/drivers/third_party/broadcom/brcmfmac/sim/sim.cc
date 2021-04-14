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
#include <zircon/time.h>

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
    .get_bootloader_macaddr =
        [](brcmf_bus* bus, uint8_t* mac_addr) {
          return BUS_OP(bus)->BusGetBootloaderMacAddr(mac_addr);
        },
    .get_wifi_metadata =
        [](brcmf_bus* bus, void* data, size_t exp_size, size_t* actual) {
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
    .preinit = [](brcmf_bus* bus) { return BUS_OP(bus)->BusPreinit(); },
    .stop = [](brcmf_bus* bus) { return BUS_OP(bus)->BusStop(); },
    .txdata = [](brcmf_bus* bus, brcmf_netbuf* netbuf) { return BUS_OP(bus)->BusTxData(netbuf); },
    .txctl = [](brcmf_bus* bus, unsigned char* msg,
                uint len) { return BUS_OP(bus)->BusTxCtl(msg, len); },
    .rxctl = [](brcmf_bus* bus, unsigned char* msg, uint len,
                int* rxlen_out) { return BUS_OP(bus)->BusRxCtl(msg, len, rxlen_out); },
    .gettxq = [](brcmf_bus* bus) { return BUS_OP(bus)->BusGetTxQueue(); },
    .flush_txq = [](brcmf_bus* bus, int ifidx) { return BUS_OP(bus)->BusFlushTxQueue(ifidx); },
    .recovery = [](brcmf_bus* bus) { return brcmf_sim_recovery(bus); },
    .log_stats = [](brcmf_bus* bus) { BRCMF_INFO("Simulated bus, no stats to log"); }};
#undef BUS_OP

// Get device-specific information
static void brcmf_sim_probe(struct brcmf_bus* bus) {
  uint32_t chip, chiprev;

  bus->bus_priv.sim->sim_fw->GetChipInfo(&chip, &chiprev);
  bus->chip = chip;
  bus->chiprev = chiprev;

  brcmf_get_module_param(BRCMF_BUS_TYPE_SIM, chip, chiprev, bus->bus_priv.sim->settings.get());
}

zx_status_t brcmf_sim_alloc(brcmf_pub* drvr, std::unique_ptr<brcmf_bus>* out_bus,
                            ::wlan::simulation::FakeDevMgr* dev_mgr,
                            std::shared_ptr<::wlan::simulation::Environment> env) {
  auto simdev = new brcmf_simdev();
  auto bus_if = std::make_unique<brcmf_bus>();

  // Initialize inter-structure pointers
  simdev->drvr = drvr;
  simdev->env = env;
  simdev->sim_fw = std::make_unique<::wlan::brcmfmac::SimFirmware>(simdev);
  simdev->dev_mgr = dev_mgr;
  simdev->settings = std::make_unique<brcmf_mp_device>();
  bus_if->bus_priv.sim = simdev;

  bus_if->ops = &brcmf_sim_bus_ops;
  drvr->bus_if = bus_if.get();
  drvr->settings = simdev->settings.get();

  *out_bus = std::move(bus_if);
  return ZX_OK;
}

zx_status_t brcmf_sim_register(brcmf_pub* drvr) {
  BRCMF_DBG(SIM, "Registering simulator target");
  brcmf_sim_probe(drvr->bus_if);

  zx_status_t status = brcmf_attach(drvr);
  if (status != ZX_OK) {
    BRCMF_ERR("brcmf_attach failed");
    return status;
  }

  status = brcmf_proto_bcdc_attach(drvr);
  if (status != ZX_OK) {
    BRCMF_ERR("brcmf_proto_bcdc_attach failed: %s", zx_status_get_string(status));
    brcmf_detach(drvr);
    return status;
  }

  // Here is where we would likely simulate loading the firmware into the target. For now,
  // we don't try.

  status = brcmf_bus_started(drvr, false);
  if (status != ZX_OK) {
    BRCMF_ERR("brcmf_bus_started failed: %s", zx_status_get_string(status));
    brcmf_detach(drvr);
    brcmf_proto_bcdc_detach(drvr);
  }
  return status;
}

// Allocate a netbuf, copy the data, and pass it to the driver, which takes ownership of the memory.
// Copying the data isn't ideal, but performance isn't an issue in the simulator and it's cleaner to
// draw a line between memory owned by the simulator and memory owned by the driver, with this file
// being the only thing that straddles that boundary.
zx_status_t brcmf_sim_create_netbuf(brcmf_simdev* simdev,
                                    std::shared_ptr<std::vector<uint8_t>> buffer,
                                    brcmf_netbuf** netbuf_out) {
  uint32_t packet_len = buffer->size();
  if (packet_len < ETH_HLEN) {
    BRCMF_ERR("Malformed packet\n");
    return ZX_ERR_INVALID_ARGS;
  }
  brcmf_netbuf* netbuf = brcmf_netbuf_allocate(packet_len);
  memcpy(netbuf->data, buffer->data(), packet_len);
  brcmf_netbuf_set_length_to(netbuf, packet_len);
  *netbuf_out = netbuf;
  return ZX_OK;
}

// Handle a simulator event
void brcmf_sim_rx_event(brcmf_simdev* simdev, std::shared_ptr<std::vector<uint8_t>> buffer) {
  brcmf_netbuf* netbuf = nullptr;
  zx_status_t status = brcmf_sim_create_netbuf(simdev, std::move(buffer), &netbuf);
  if (status == ZX_OK) {
    brcmf_rx_event(simdev->drvr, netbuf);
  }
}

// Handle a simulator frame
void brcmf_sim_rx_frame(brcmf_simdev* simdev, std::shared_ptr<std::vector<uint8_t>> buffer) {
  brcmf_netbuf* netbuf = nullptr;
  zx_status_t status = brcmf_sim_create_netbuf(simdev, std::move(buffer), &netbuf);
  if (status == ZX_OK) {
    brcmf_rx_frame(simdev->drvr, netbuf, false);
  }
}

zx_status_t brcmf_sim_recovery(brcmf_bus* bus) {
  brcmf_simdev* simdev = bus->bus_priv.sim;

  // Go through the recovery process in SIM bus(Here we just do firmware reset
  // instead of firmware reload).
  simdev->sim_fw = std::make_unique<::wlan::brcmfmac::SimFirmware>(simdev);
  return ZX_OK;
}

void brcmf_sim_firmware_crash(brcmf_simdev* simdev) {
  brcmf_pub* drvr = simdev->drvr;
  zx_status_t err = drvr->recovery_trigger->firmware_crash_.Inc();
  if (err != ZX_OK) {
    BRCMF_ERR("Increase recovery trigger condition failed -- error: %s", zx_status_get_string(err));
  }
  // Clear the counters of all TriggerConditions here instead of inside brcmf_recovery_worker() to
  // break deadlock.
  drvr->recovery_trigger->ClearStatistics();
}

void brcmf_sim_exit(brcmf_bus* bus) {
  brcmf_detach((bus->bus_priv.sim)->drvr);
  brcmf_proto_bcdc_detach((bus->bus_priv.sim)->drvr);
  delete bus->bus_priv.sim;
  bus->bus_priv.sim = nullptr;
}
