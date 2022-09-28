// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is to fake the PCIe transportation layer and the firmware
// in order to test upper layers of the iwlwifi driver.
//
// The simulated behaviors are implemented in the 'trans_ops_sim_trans',
// which is a 'struct iwl_trans_ops'.
//
// This file also calls device_add() to register 'wlanphy_impl_protocol_ops_t'
// so that we can simulate the MLME (the user of this softmac driver) to test
// the iface functions.
//
// After iwl_sim_trans_transport_alloc() is called, memory containing iwl_trans +
// sim_trans_priv is returned. To access the simulated-transportation-specific
// variable, use IWL_TRANS_GET_SIM_TRANS(trans) to get it.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-trans.h"

#include <fuchsia/hardware/wlan/softmac/c/banjo.h>
#include <fuchsia/hardware/wlanphyimpl/c/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fake-bti/bti.h>
#include <zircon/status.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/alive.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/commands.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-config.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-drv.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/mvm-mlme.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/rcu-manager.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/wlanphy-impl-device.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

using wlan::testing::IWL_TRANS_GET_SIM_TRANS;
using wlan::testing::sim_trans_priv;
using wlan::testing::SimMvm;

namespace {

// SimTransDevice to appropriately handle unbind and release.
class SimTransDevice : public ::wlan::iwlwifi::WlanphyImplDevice {
 public:
  explicit SimTransDevice(zx_device_t* parent, iwl_trans* drvdata)
      : WlanphyImplDevice(parent), drvdata_(drvdata) {}
  void DdkInit(::ddk::InitTxn txn) override { txn.Reply(ZX_OK); }
  void DdkUnbind(::ddk::UnbindTxn txn) override {
    // Saving the input UnbindTxn to the device, ::ddk::UnbindTxn::Reply() will be called with this
    // UnbindTxn in the shutdown callback of the dispatcher, so that we can make sure DdkUnbind()
    // won't end before the dispatcher shutdown.
    unbind_txn_ = std::move(txn);
    struct iwl_trans* trans = drvdata_;
    if (trans->drv) {
      iwl_drv_stop(trans->drv);
    }
    free(trans);
    dispatcher_.ShutdownAsync();
  }

  iwl_trans* drvdata() override { return drvdata_; }
  const iwl_trans* drvdata() const override { return drvdata_; }

 private:
  iwl_trans* drvdata_ = nullptr;
};

}  // namespace

// Send a fake packet from FW to unblock one wait in mvm->notif_wait.
static void rx_fw_notification(struct iwl_trans* trans, uint8_t cmd, const void* data,
                               size_t size) {
  struct iwl_mvm* mvm = IWL_OP_MODE_GET_MVM(trans->op_mode);
  iwl_rx_packet pkt = {};
  pkt.len_n_flags = (size & FH_RSCSR_FRAME_SIZE_MSK) + sizeof(pkt.hdr);
  pkt.hdr.cmd = cmd;

  std::string buffer(sizeof(pkt) + size, '\0');
  std::memcpy(buffer.data(), &pkt, sizeof(pkt));
  std::memcpy(buffer.data() + sizeof(pkt), data, size);
  iwl_notification_wait_notify(&mvm->notif_wait, reinterpret_cast<iwl_rx_packet*>(buffer.data()));
}

// Notify the mvm->notif_wait to unblock the waiting.
static void unblock_notif_wait(struct iwl_trans* trans) {
  struct iwl_mvm* mvm = IWL_OP_MODE_GET_MVM(trans->op_mode);
  iwl_notification_notify(&mvm->notif_wait);
}

static zx_status_t iwl_sim_trans_start_hw(struct iwl_trans* iwl_trans, bool low_power) {
  return ZX_OK;
}

static void iwl_sim_trans_op_mode_leave(struct iwl_trans* iwl_trans) {}

static zx_status_t iwl_sim_trans_start_fw(struct iwl_trans* trans, const struct fw_img* fw,
                                          bool run_in_rfkill) {
  // Kick off the firmware.
  //
  // Since we don't have a real firmware to load, there will be no notification from the firmware.
  // Fake a RX packet's behavior so that we won't get blocked in the iwl_mvm_mac_start().
  mvm_alive_resp resp = {};
  resp.status = htole16(IWL_ALIVE_STATUS_OK);
  rx_fw_notification(trans, iwl_legacy_cmds::MVM_ALIVE, &resp, sizeof(resp));

  return ZX_OK;
}

static void iwl_sim_trans_fw_alive(struct iwl_trans* trans, uint32_t scd_addr) {}

static void iwl_sim_trans_stop_device(struct iwl_trans* trans, bool low_power) {}

static zx_status_t iwl_sim_trans_send_cmd(struct iwl_trans* trans, struct iwl_host_cmd* cmd) {
  bool notify_wait;
  zx_status_t ret = IWL_TRANS_GET_SIM_TRANS(trans)->fw->SendCmd(trans, cmd, &notify_wait);

  // On real hardware, some particular commands would reply a packet to unblock the wait.
  // However, in the simulated firmware, we don't generate the packet. We unblock it directly.
  if (notify_wait) {
    unblock_notif_wait(trans);
  }

  return ret;
}

static zx_status_t iwl_sim_trans_tx(struct iwl_trans* trans, ieee80211_mac_packet* pkt,
                                    const struct iwl_device_cmd* dev_cmd, int queue) {
  return ZX_ERR_NOT_SUPPORTED;
}

static void iwl_sim_trans_reclaim(struct iwl_trans* trans, int queue, int ssn) {}

static bool iwl_sim_trans_txq_enable(struct iwl_trans* trans, int queue, uint16_t ssn,
                                     const struct iwl_trans_txq_scd_cfg* cfg,
                                     zx_duration_t queue_wdg_timeout) {
  return false;
}

static void iwl_sim_trans_txq_disable(struct iwl_trans* trans, int queue, bool configure_scd) {}

static void iwl_sim_trans_write8(struct iwl_trans* trans, uint32_t ofs, uint8_t val) {}

static void iwl_sim_trans_write32(struct iwl_trans* trans, uint32_t ofs, uint32_t val) {}

static uint32_t iwl_sim_trans_read32(struct iwl_trans* trans, uint32_t ofs) {
  return __INT32_MAX__;
}

static uint32_t iwl_sim_trans_read_prph(struct iwl_trans* trans, uint32_t ofs) {
  return __INT32_MAX__;
}

static void iwl_sim_trans_write_prph(struct iwl_trans* trans, uint32_t ofs, uint32_t val) {}

static zx_status_t iwl_sim_trans_read_mem(struct iwl_trans* trans, uint32_t addr, void* buf,
                                          size_t dwords) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t iwl_sim_trans_write_mem(struct iwl_trans* trans, uint32_t addr, const void* buf,
                                           size_t dwords) {
  return ZX_ERR_NOT_SUPPORTED;
}

static void iwl_sim_trans_configure(struct iwl_trans* trans,
                                    const struct iwl_trans_config* trans_cfg) {}

static void iwl_sim_trans_set_pmi(struct iwl_trans* trans, bool state) {}

static void iwl_sim_trans_sw_reset(struct iwl_trans* trans) {}

static bool iwl_sim_trans_grab_nic_access(struct iwl_trans* trans, unsigned long* flags) {
  return false;
}

static void iwl_sim_trans_release_nic_access(struct iwl_trans* trans, unsigned long* flags) {}

static void iwl_sim_trans_set_bits_mask(struct iwl_trans* trans, uint32_t reg, uint32_t mask,
                                        uint32_t value) {}

static void iwl_sim_trans_ref(struct iwl_trans* trans) {}

static void iwl_sim_trans_unref(struct iwl_trans* trans) {}

static zx_status_t iwl_sim_trans_suspend(struct iwl_trans* trans) { return ZX_ERR_NOT_SUPPORTED; }

static void iwl_sim_trans_resume(struct iwl_trans* trans) {}

static zx_status_t iwl_sim_trans_wait_tx_queues_empty(struct iwl_trans* trans, uint32_t txq_bm) {
  return ZX_OK;
}

static zx_status_t iwl_sim_trans_wait_txq_empty(struct iwl_trans* trans, int queue) {
  return ZX_OK;
}

static struct iwl_trans_ops trans_ops_sim_trans = {
    .start_hw = iwl_sim_trans_start_hw,
    .op_mode_leave = iwl_sim_trans_op_mode_leave,
    .start_fw = iwl_sim_trans_start_fw,
    .fw_alive = iwl_sim_trans_fw_alive,
    .stop_device = iwl_sim_trans_stop_device,
    .send_cmd = iwl_sim_trans_send_cmd,
    .tx = iwl_sim_trans_tx,
    .reclaim = iwl_sim_trans_reclaim,
    .txq_enable = iwl_sim_trans_txq_enable,
    .txq_disable = iwl_sim_trans_txq_disable,
    .wait_tx_queues_empty = iwl_sim_trans_wait_tx_queues_empty,
    .wait_txq_empty = iwl_sim_trans_wait_txq_empty,
    .write8 = iwl_sim_trans_write8,
    .write32 = iwl_sim_trans_write32,
    .read32 = iwl_sim_trans_read32,
    .read_prph = iwl_sim_trans_read_prph,
    .write_prph = iwl_sim_trans_write_prph,
    .read_mem = iwl_sim_trans_read_mem,
    .write_mem = iwl_sim_trans_write_mem,
    .configure = iwl_sim_trans_configure,
    .set_pmi = iwl_sim_trans_set_pmi,
    .sw_reset = iwl_sim_trans_sw_reset,
    .grab_nic_access = iwl_sim_trans_grab_nic_access,
    .release_nic_access = iwl_sim_trans_release_nic_access,
    .set_bits_mask = iwl_sim_trans_set_bits_mask,
    .ref = iwl_sim_trans_ref,
    .unref = iwl_sim_trans_unref,
    .suspend = iwl_sim_trans_suspend,
    .resume = iwl_sim_trans_resume,

#if 0   // NEEDS_PORTING
    void (*d3_suspend)(struct iwl_trans* trans, bool test, bool reset);
    int (*d3_resume)(struct iwl_trans* trans, enum iwl_d3_status* status, bool test, bool reset);

    /* 22000 functions */
    int (*txq_alloc)(struct iwl_trans* trans, __le16 flags, uint8_t sta_id, uint8_t tid, int cmd_id,
                     int size, unsigned int queue_wdg_timeout);
    void (*txq_free)(struct iwl_trans* trans, int queue);
    int (*rxq_dma_data)(struct iwl_trans* trans, int queue, struct iwl_trans_rxq_dma_data* data);

    void (*txq_set_shared_mode)(struct iwl_trans* trans, uint32_t txq_id, bool shared);

    void (*freeze_txq_timer)(struct iwl_trans* trans, unsigned long txqs, bool freeze);
    void (*block_txq_ptrs)(struct iwl_trans* trans, bool block);

    struct iwl_trans_dump_data* (*dump_data)(struct iwl_trans* trans, uint32_t dump_mask);
    void (*debugfs_cleanup)(struct iwl_trans* trans);
#endif  // NEEDS_PORTING
};

// iwl_trans_alloc() will allocate memory containing iwl_trans + sim_trans_priv.
static struct iwl_trans* iwl_sim_trans_transport_alloc(struct device* dev,
                                                       const struct iwl_cfg* cfg, SimMvm* fw) {
  struct iwl_trans* iwl_trans =
      iwl_trans_alloc(sizeof(struct sim_trans_priv), dev, cfg, &trans_ops_sim_trans);

  IWL_TRANS_GET_SIM_TRANS(iwl_trans)->fw = fw;

  return iwl_trans;
}

// This function intends to be like this because we want to mimic the transport_pcie_bind().
// But definitely can be refactored into the SimTransport::Init().
// 'out_trans' is used to return the new allocated 'struct iwl_trans'.
static zx_status_t sim_transport_bind(SimMvm* fw, struct device* dev,
                                      struct iwl_trans** out_iwl_trans,
                                      wlan::iwlwifi::WlanphyImplDevice** out_device) {
  zx_status_t status = ZX_OK;
  const struct iwl_cfg* cfg = &iwl7265_2ac_cfg;

  struct iwl_trans* iwl_trans = iwl_sim_trans_transport_alloc(dev, cfg, fw);
  if (!iwl_trans) {
    return ZX_ERR_INTERNAL;
  }
  ZX_ASSERT(out_iwl_trans);

  auto device = std::make_unique<SimTransDevice>(dev->zxdev, iwl_trans);
  status = device->DdkAdd("sim-iwlwifi-wlanphyimpl", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to add wlanphyimpl device: %s", zx_status_get_string(status));
    return status;
  }
  iwl_trans->zxdev = device->zxdev();

  status = iwl_drv_init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to init driver: %s", zx_status_get_string(status));
    goto remove_dev;
  }

  status = iwl_drv_start(iwl_trans, &iwl_trans->drv);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to start driver: %s", zx_status_get_string(status));
    goto remove_dev;
  }

  status = iwl_mvm_mac_start(IWL_OP_MODE_GET_MVM(iwl_trans->op_mode));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to start mac: %s", zx_status_get_string(status));
    goto remove_dev;
  }

  *out_iwl_trans = iwl_trans;
  *out_device = device.release();

  return ZX_OK;

remove_dev:
  device.release()->DdkAsyncRemove();
  return status;
}

namespace wlan::testing {

SimTransport::SimTransport(zx_device_t* parent) : device_{}, iwl_trans_(nullptr) {
  task_loop_ = std::make_unique<::async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  task_loop_->StartThread("iwlwifi-test-task-worker", nullptr);
  irq_loop_ = std::make_unique<::async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  irq_loop_->StartThread("iwlwifi-test-irq-worker", nullptr);
  rcu_manager_ = std::make_unique<::wlan::iwlwifi::RcuManager>(task_loop_->dispatcher());

  device_.zxdev = parent;
  device_.task_dispatcher = task_loop_->dispatcher();
  device_.irq_dispatcher = irq_loop_->dispatcher();
  device_.rcu_manager = static_cast<struct rcu_manager*>(rcu_manager_.get());
  fake_bti_create(&device_.bti);
}

SimTransport::~SimTransport() {
  if (sim_device_) {
    sim_device_->DdkAsyncRemove();
    mock_ddk::ReleaseFlaggedDevices(sim_device_->zxdev());
  }
  zx_handle_close(device_.bti);
}

zx_status_t SimTransport::Init() {
  return sim_transport_bind(this, &device_, &iwl_trans_, &sim_device_);
}

struct iwl_trans* SimTransport::iwl_trans() { return iwl_trans_; }

const struct iwl_trans* SimTransport::iwl_trans() const { return iwl_trans_; }

::wlan::iwlwifi::WlanphyImplDevice* SimTransport::sim_device() { return sim_device_; }

const ::wlan::iwlwifi::WlanphyImplDevice* SimTransport::sim_device() const { return sim_device_; }

zx_device_t* SimTransport::fake_parent() { return device_.zxdev; }

}  // namespace wlan::testing
