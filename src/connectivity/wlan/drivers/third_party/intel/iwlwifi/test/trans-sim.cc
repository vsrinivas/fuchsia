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
//
// After iwl_trans_transport_sim_alloc() is called, memory containing iwl_trans +
// trans_sim_priv is returned. To access the simulated-transportation-specific
// variable, use IWL_TRANS_GET_TRANS_SIM(trans) to get it.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/trans-sim.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <zircon/status.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/wlanphyimpl.h>
#include <wlan/protocol/mac.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-config.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-drv.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/wlan-device.h"
}

using wlan::testing::IWL_TRANS_GET_TRANS_SIM;
using wlan::testing::SimMvm;
using wlan::testing::trans_sim_priv;

// Notify the mvm->notif_wait to unblock the waiting.
static void unblock_notif_wait(struct iwl_trans* trans) {
  struct iwl_mvm* mvm = IWL_OP_MODE_GET_MVM(trans->op_mode);
  iwl_notification_notify(&mvm->notif_wait);
}

static zx_status_t iwl_trans_sim_start_hw(struct iwl_trans* iwl_trans, bool low_power) {
  return ZX_OK;
}

static void iwl_trans_sim_op_mode_leave(struct iwl_trans* iwl_trans) {}

static zx_status_t iwl_trans_sim_start_fw(struct iwl_trans* trans, const struct fw_img* fw,
                                          bool run_in_rfkill) {
  // Kick off the firmware.
  //
  // Since we don't have a real firmware to load, there will be no notification from the firmware.
  // Fake a RX packet's behavior so that we won't get blocked in the iwl_mvm_mac_start().
  unblock_notif_wait(trans);

  return ZX_OK;
}

static void iwl_trans_sim_fw_alive(struct iwl_trans* trans, uint32_t scd_addr) {}

static void iwl_trans_sim_stop_device(struct iwl_trans* trans, bool low_power) {}

static zx_status_t iwl_trans_sim_send_cmd(struct iwl_trans* trans, struct iwl_host_cmd* cmd) {
  bool notify_wait;
  zx_status_t ret = IWL_TRANS_GET_TRANS_SIM(trans)->fw->SendCmd(cmd, &notify_wait);

  // On real hardware, some particular commands would reply a packet to unblock the wait.
  // However, in the simulated firmware, we don't generate the packet. We unblock it directly.
  if (notify_wait) {
    unblock_notif_wait(trans);
  }

  return ret;
}

static zx_status_t iwl_trans_sim_tx(struct iwl_trans* trans, struct sk_buff* skb,
                                    struct iwl_device_cmd* dev_cmd, int queue) {
  return ZX_ERR_NOT_SUPPORTED;
}

static void iwl_trans_sim_reclaim(struct iwl_trans* trans, int queue, int ssn,
                                  struct sk_buff_head* skbs) {}

static bool iwl_trans_sim_txq_enable(struct iwl_trans* trans, int queue, uint16_t ssn,
                                     const struct iwl_trans_txq_scd_cfg* cfg,
                                     zx_duration_t queue_wdg_timeout) {
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

#if 0  // NEEDS_PORTING
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

// iwl_trans_alloc() will allocate memory containing iwl_trans + trans_sim_priv.
static struct iwl_trans* iwl_trans_transport_sim_alloc(const struct iwl_cfg* cfg, SimMvm* fw) {
  struct iwl_trans* iwl_trans =
      iwl_trans_alloc(sizeof(struct trans_sim_priv), cfg, &trans_ops_trans_sim);

  IWL_TRANS_GET_TRANS_SIM(iwl_trans)->fw = fw;

  return iwl_trans;
}

static void transport_sim_unbind(void* ctx) {
  struct iwl_trans* trans = (struct iwl_trans*)ctx;
  device_unbind_reply(trans->zxdev);
}

static void transport_sim_release(void* ctx) {
  struct iwl_trans* trans = (struct iwl_trans*)ctx;

  iwl_drv_stop(trans->drv);

  free(trans);
}

// TODO(fxbug.dev/36795): move to wlan-device.c
static zx_protocol_device_t device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = transport_sim_unbind,
    .release = transport_sim_release,
};

// This function intends to be like this because we want to mimic the transport_pcie_bind().
// But definitely can be refactored into the TransportSim::Init().
// 'out_trans' is used to return the new allocated 'struct iwl_trans'.
static zx_status_t transport_sim_bind(SimMvm* fw, zx_device_t* dev,
                                      struct iwl_trans** out_iwl_trans) {
  const struct iwl_cfg* cfg = &iwl7265_2ac_cfg;
  struct iwl_trans* iwl_trans = iwl_trans_transport_sim_alloc(cfg, fw);
  iwl_trans->to_load_firmware = false;
  zx_status_t status;

  if (!iwl_trans) {
    return ZX_ERR_INTERNAL;
  }
  ZX_ASSERT(out_iwl_trans);

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "sim-iwlwifi-wlanphy",
      .ctx = iwl_trans,
      .ops = &device_ops,
      .proto_id = ZX_PROTOCOL_WLANPHY_IMPL,
      .proto_ops = &wlanphy_ops,
      .flags = DEVICE_ADD_NON_BINDABLE,
  };

  status = device_add(dev, &args, &iwl_trans->zxdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to create device: %s", zx_status_get_string(status));
    free(iwl_trans);
    return status;
  }

  status = iwl_drv_init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to init driver: %s", zx_status_get_string(status));
    goto remove_dev;
  }

  status = iwl_drv_start(iwl_trans);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to start driver: %s", zx_status_get_string(status));
    goto remove_dev;
  }

  // Manually add the driver to the MVM opmode so that iwl_mvm_init() can call the
  // corresponding start function (iwl_op_mode_mvm_start()).
  iwl_drv_add_to_mvm_opmode(iwl_trans->drv);
  status = iwl_mvm_init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to init MVM: %s", zx_status_get_string(status));
    goto remove_dev;
  }

  {
    *out_iwl_trans = iwl_trans;
    struct iwl_mvm* mvm = IWL_OP_MODE_GET_MVM(iwl_trans->op_mode);
    return iwl_mvm_mac_start(mvm);
  }

remove_dev:
  device_async_remove(iwl_trans->zxdev);

  return status;
}

namespace wlan {
namespace testing {

zx_status_t TransportSim::Init() {
  return transport_sim_bind(this, fake_ddk::kFakeParent, &iwl_trans_);
}

void TransportSim::Release() { transport_sim_release(iwl_trans_); }

}  // namespace testing
}  // namespace wlan
