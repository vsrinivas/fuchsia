// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is to fake the PCIe transportation layer and the firmware
// in order to test upper layers of the iwlwifi driver.
//
// The simulated behaviors are implemented in the 'trans_ops_trans_sim',
// which is a 'struct iwl_trans_ops'.
//
// This file also calls device_add() to register 'wlanphy_impl_protocol_ops_t'
// so that we can simulate the MLME (the user of this softmac driver) to test
// the iface functions.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/trans-sim.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/wlanphyimpl.h>
#include <wlan/protocol/mac.h>
#include <zircon/status.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-config.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-drv.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"
}

static zx_status_t iwl_trans_sim_start_hw(struct iwl_trans* iwl_trans, bool low_power) {
  return ZX_ERR_NOT_SUPPORTED;
}

static void iwl_trans_sim_op_mode_leave(struct iwl_trans* iwl_trans) {}

static zx_status_t iwl_trans_sim_start_fw(struct iwl_trans* trans, const struct fw_img* fw,
                                          bool run_in_rfkill) {
  return ZX_ERR_NOT_SUPPORTED;
}

static void iwl_trans_sim_fw_alive(struct iwl_trans* trans, uint32_t scd_addr) {}

static void iwl_trans_sim_stop_device(struct iwl_trans* trans, bool low_power) {}

static zx_status_t iwl_trans_sim_send_cmd(struct iwl_trans* trans, struct iwl_host_cmd* cmd) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t iwl_trans_sim_tx(struct iwl_trans* trans, struct sk_buff* skb,
                                    struct iwl_device_cmd* dev_cmd, int queue) {
  return ZX_ERR_NOT_SUPPORTED;
}

static void iwl_trans_sim_reclaim(struct iwl_trans* trans, int queue, int ssn,
                                  struct sk_buff_head* skbs) {}

static bool iwl_trans_sim_txq_enable(struct iwl_trans* trans, int queue, uint16_t ssn,
                                     const struct iwl_trans_txq_scd_cfg* cfg,
                                     unsigned int queue_wdg_timeout) {
  return false;
}

static void iwl_trans_sim_txq_disable(struct iwl_trans* trans, int queue, bool configure_scd) {}

static void iwl_trans_sim_write8(struct iwl_trans* trans, uint32_t ofs, uint8_t val) {}

static void iwl_trans_sim_write32(struct iwl_trans* trans, uint32_t ofs, uint32_t val) {}

static uint32_t iwl_trans_sim_read32(struct iwl_trans* trans, uint32_t ofs) {
  return __INT32_MAX__;
}

static uint32_t iwl_trans_sim_read_prph(struct iwl_trans* trans, uint32_t ofs) {
  return __INT32_MAX__;
}

static void iwl_trans_sim_write_prph(struct iwl_trans* trans, uint32_t ofs, uint32_t val) {}

static zx_status_t iwl_trans_sim_read_mem(struct iwl_trans* trans, uint32_t addr, void* buf,
                                          int dwords) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t iwl_trans_sim_write_mem(struct iwl_trans* trans, uint32_t addr, const void* buf,
                                           int dwords) {
  return ZX_ERR_NOT_SUPPORTED;
}

static void iwl_trans_sim_configure(struct iwl_trans* trans,
                                    const struct iwl_trans_config* trans_cfg) {}

static void iwl_trans_sim_set_pmi(struct iwl_trans* trans, bool state) {}

static void iwl_trans_sim_sw_reset(struct iwl_trans* trans) {}

static bool iwl_trans_sim_grab_nic_access(struct iwl_trans* trans, unsigned long* flags) {
  return false;
}

static void iwl_trans_sim_release_nic_access(struct iwl_trans* trans, unsigned long* flags) {}

static void iwl_trans_sim_set_bits_mask(struct iwl_trans* trans, uint32_t reg, uint32_t mask,
                                        uint32_t value) {}

static void iwl_trans_sim_ref(struct iwl_trans* trans) {}

static void iwl_trans_sim_unref(struct iwl_trans* trans) {}

static zx_status_t iwl_trans_sim_suspend(struct iwl_trans* trans) { return ZX_ERR_NOT_SUPPORTED; }

static void iwl_trans_sim_resume(struct iwl_trans* trans) {}

static struct iwl_trans_ops trans_ops_trans_sim = {
    .start_hw = iwl_trans_sim_start_hw,
    .op_mode_leave = iwl_trans_sim_op_mode_leave,
    .start_fw = iwl_trans_sim_start_fw,
    .fw_alive = iwl_trans_sim_fw_alive,
    .stop_device = iwl_trans_sim_stop_device,
    .send_cmd = iwl_trans_sim_send_cmd,
    .tx = iwl_trans_sim_tx,
    .reclaim = iwl_trans_sim_reclaim,
    .txq_enable = iwl_trans_sim_txq_enable,
    .txq_disable = iwl_trans_sim_txq_disable,
    .write8 = iwl_trans_sim_write8,
    .write32 = iwl_trans_sim_write32,
    .read32 = iwl_trans_sim_read32,
    .read_prph = iwl_trans_sim_read_prph,
    .write_prph = iwl_trans_sim_write_prph,
    .read_mem = iwl_trans_sim_read_mem,
    .write_mem = iwl_trans_sim_write_mem,
    .configure = iwl_trans_sim_configure,
    .set_pmi = iwl_trans_sim_set_pmi,
    .sw_reset = iwl_trans_sim_sw_reset,
    .grab_nic_access = iwl_trans_sim_grab_nic_access,
    .release_nic_access = iwl_trans_sim_release_nic_access,
    .set_bits_mask = iwl_trans_sim_set_bits_mask,
    .ref = iwl_trans_sim_ref,
    .unref = iwl_trans_sim_unref,
    .suspend = iwl_trans_sim_suspend,
    .resume = iwl_trans_sim_resume,

#if 0   // NEEDS_PORTING
    void (*d3_suspend)(struct iwl_trans* trans, bool test, bool reset);
    int (*d3_resume)(struct iwl_trans* trans, enum iwl_d3_status* status, bool test, bool reset);

    /* 22000 functions */
    int (*txq_alloc)(struct iwl_trans* trans, __le16 flags, uint8_t sta_id, uint8_t tid, int cmd_id,
                     int size, unsigned int queue_wdg_timeout);
    void (*txq_free)(struct iwl_trans* trans, int queue);
    int (*rxq_dma_data)(struct iwl_trans* trans, int queue, struct iwl_trans_rxq_dma_data* data);

    void (*txq_set_shared_mode)(struct iwl_trans* trans, uint32_t txq_id, bool shared);

    int (*wait_tx_queues_empty)(struct iwl_trans* trans, uint32_t txq_bm);
    int (*wait_txq_empty)(struct iwl_trans* trans, int queue);
    void (*freeze_txq_timer)(struct iwl_trans* trans, unsigned long txqs, bool freeze);
    void (*block_txq_ptrs)(struct iwl_trans* trans, bool block);

    struct iwl_trans_dump_data* (*dump_data)(struct iwl_trans* trans, uint32_t dump_mask);
    void (*debugfs_cleanup)(struct iwl_trans* trans);
#endif  // NEEDS_PORTING
};

// The struct to store the internal state of the simulated firmware.
struct iwl_trans_trans_sim {
  uint8_t dummy;
};

static struct iwl_trans* iwl_trans_transport_sim_alloc(const struct iwl_cfg* cfg) {
  return iwl_trans_alloc(sizeof(struct iwl_trans_trans_sim), cfg, &trans_ops_trans_sim);
}

static void transport_sim_unbind(void* ctx) {
  struct iwl_trans* trans = (struct iwl_trans*)ctx;
  device_remove(trans->zxdev);
}

static void transport_sim_release(void* ctx) {
  struct iwl_trans* trans = (struct iwl_trans*)ctx;

  iwl_drv_stop(trans->drv);

  free(trans);
}

static zx_protocol_device_t device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = transport_sim_unbind,
    .release = transport_sim_release,
};

static wlanphy_impl_protocol_ops_t wlanphy_ops = {
    .query = NULL,
    .create_iface = NULL,
    .destroy_iface = NULL,
};

static zx_status_t transport_sim_bind(void* ctx, zx_device_t* dev) {
  const struct iwl_cfg* cfg = &iwl7265_2ac_cfg;
  struct iwl_trans* iwl_trans = iwl_trans_transport_sim_alloc(cfg);
  zx_status_t status;

  if (iwl_trans != nullptr) {
    return ZX_ERR_INTERNAL;
  }

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "sim-iwlwifi-wlanphy",
      .ctx = iwl_trans,
      .ops = &device_ops,
      .proto_id = ZX_PROTOCOL_WLANPHY_IMPL,
      .proto_ops = &wlanphy_ops,
      .flags = DEVICE_ADD_INVISIBLE,
  };

  status = device_add(dev, &args, &iwl_trans->zxdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to create device: %s\n", zx_status_get_string(status));
    free(iwl_trans);
    return status;
  }

  status = iwl_drv_start(iwl_trans);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to start driver: %s\n", zx_status_get_string(status));
    device_remove(iwl_trans->zxdev);
  }

  return status;
}

namespace wlan {
namespace testing {

TransportSim::TransportSim(SimulatedEnvironment* env) : SimulatedFirmware(env) {
  // 'ctx' and 'dev' are not used in the simulated firmware yet.
  transport_sim_bind(nullptr, nullptr);
}

}  // namespace testing
}  // namespace wlan
