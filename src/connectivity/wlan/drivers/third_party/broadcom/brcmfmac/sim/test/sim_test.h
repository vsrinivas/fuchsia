// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_TEST_SIM_TEST_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_TEST_SIM_TEST_H_

#include <zircon/types.h>

#include <set>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim_device.h"

namespace wlan::brcmfmac {

// This class represents an interface created on a simulated device, collecting all of the
// attributes related to that interface.
class SimInterface {
 public:
  // Track state of association
  struct AssocContext {
    enum AssocState {
      kNone,
      kJoining,
      kAuthenticating,
      kAssociating,
      kAssociated,
    } state = kNone;

    common::MacAddr bssid;
    wlan_ssid_t ssid;
    wlan_channel_t channel;
  };

  struct SoftApContext {
    wlan_ssid_t ssid;
  };

  // Useful statistics about operations performed
  struct Stats {
    size_t assoc_attempts = 0;
    size_t assoc_successes = 0;
    std::list<wlan_join_result_t> join_results;
    std::list<wlanif_auth_confirm_t> auth_results;
    std::list<wlanif_assoc_confirm_t> assoc_results;
    std::list<wlanif_assoc_ind_t> assoc_indications;
    std::list<wlanif_auth_ind_t> auth_indications;
    std::list<wlanif_deauth_confirm_t> deauth_results;
    std::list<wlanif_deauth_indication_t> deauth_indications;
    std::list<wlanif_disassoc_indication_t> disassoc_indications;
    std::list<wlanif_channel_switch_info_t> csa_indications;
    std::list<wlanif_start_confirm_t> start_confirmations;
    std::list<wlanif_stop_confirm_t> stop_confirmations;
  };

  // Default scan options
  static const std::vector<uint8_t> kDefaultScanChannels;
  static constexpr uint32_t kDefaultActiveScanDwellTimeMs = 40;
  static constexpr uint32_t kDefaultPassiveScanDwellTimeMs = 120;

  // SoftAP defaults
  static constexpr wlan_ssid_t kDefaultSoftApSsid = {.len = 10, .ssid = "Sim_SoftAP"};
  static constexpr wlan_channel_t kDefaultSoftApChannel = {
      .primary = 11, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
  static constexpr uint32_t kDefaultSoftApBeaconPeriod = 100;
  static constexpr uint32_t kDefaultSoftApDtimPeriod = 100;

  SimInterface() = default;
  SimInterface(const SimInterface&) = delete;

  zx_status_t Init(std::shared_ptr<simulation::Environment> env, wlan_info_mac_role_t role);

  virtual ~SimInterface() {
    if (ch_sme_ != ZX_HANDLE_INVALID) {
      zx_handle_close(ch_sme_);
    }
    if (ch_mlme_ != ZX_HANDLE_INVALID) {
      zx_handle_close(ch_mlme_);
    }
  }

  // Default SME Callbacks
  virtual void OnScanResult(const wlanif_scan_result_t* result);
  virtual void OnScanEnd(const wlanif_scan_end_t* end);
  virtual void OnJoinConf(const wlanif_join_confirm_t* resp);
  virtual void OnAuthConf(const wlanif_auth_confirm_t* resp);
  virtual void OnAuthInd(const wlanif_auth_ind_t* resp);
  virtual void OnDeauthConf(const wlanif_deauth_confirm_t* resp);
  virtual void OnDeauthInd(const wlanif_deauth_indication_t* ind);
  virtual void OnAssocConf(const wlanif_assoc_confirm_t* resp);
  virtual void OnAssocInd(const wlanif_assoc_ind_t* ind);
  virtual void OnDisassocConf(const wlanif_disassoc_confirm_t* resp) {}
  virtual void OnDisassocInd(const wlanif_disassoc_indication_t* ind);
  virtual void OnStartConf(const wlanif_start_confirm_t* resp);
  virtual void OnStopConf(const wlanif_stop_confirm_t* resp);
  virtual void OnEapolConf(const wlanif_eapol_confirm_t* resp) {}
  virtual void OnChannelSwitch(const wlanif_channel_switch_info_t* ind);
  virtual void OnSignalReport(const wlanif_signal_report_indication_t* ind) {}
  virtual void OnEapolInd(const wlanif_eapol_indication_t* ind) {}
  virtual void OnStatsQueryResp(const wlanif_stats_query_response_t* resp) {}
  virtual void OnRelayCapturedFrame(const wlanif_captured_frame_result_t* result) {}
  virtual void OnDataRecv(const void* data, size_t data_size, uint32_t flags) {}

  // Query an interface
  void Query(wlanif_query_info_t* out_info);

  // Stop an interface
  void StopInterface();

  // Get the Mac address of an interface
  void GetMacAddr(common::MacAddr* out_macaddr);

  // Start an assocation with a fake AP. We can use these for subsequent association events, but
  // not interleaved association events (which I doubt are terribly useful, anyway). Note that for
  // the moment only non-authenticated associations are supported.
  void StartAssoc(const common::MacAddr& bssid, const wlan_ssid_t& ssid,
                  const wlan_channel_t& channel);
  void AssociateWith(const simulation::FakeAp& ap,
                     std::optional<zx::duration> delay = std::nullopt);

  void DeauthenticateFrom(const common::MacAddr& bssid,
                          wlan_deauth_reason_t reason = WLAN_DEAUTH_REASON_UNSPECIFIED);

  // Scan operations
  void StartScan(uint64_t txn_id = 0, bool active = false);
  std::optional<wlan_scan_result_t> ScanResultCode(uint64_t txn_id);
  const std::list<wlanif_bss_description_t>* ScanResultBssList(uint64_t txn_id);

  // SoftAP operation
  void StartSoftAp(const wlan_ssid_t& ssid = kDefaultSoftApSsid,
                   const wlan_channel_t& channel = kDefaultSoftApChannel,
                   uint32_t beacon_period = kDefaultSoftApBeaconPeriod,
                   uint32_t dtim_period = kDefaultSoftApDtimPeriod);
  void StopSoftAp();

  zx_status_t SetMulticastPromisc(bool enable);

  std::shared_ptr<simulation::Environment> env_;

  static wlanif_impl_ifc_protocol_ops_t default_sme_dispatch_tbl_;
  wlanif_impl_ifc_protocol default_ifc_ = {.ops = &default_sme_dispatch_tbl_, .ctx = this};

  // This provides our DDK (wlanif-impl) API into the interface
  void* if_impl_ctx_;
  wlanif_impl_protocol_ops_t* if_impl_ops_;

  // Unique identifier provided by the driver
  uint16_t iface_id_;

  // Handles for SME <=> MLME communication, required but never used for communication (since no
  // SME is present).
  zx_handle_t ch_sme_ = ZX_HANDLE_INVALID;   // SME-owned side
  zx_handle_t ch_mlme_ = ZX_HANDLE_INVALID;  // MLME-owned side

  // Current state of association
  AssocContext assoc_ctx_;

  // Current state of soft AP
  SoftApContext soft_ap_ctx_;

  // Allows us to track individual operations
  Stats stats_;

 private:
  wlan_info_mac_role_t role_ = 0;

  // Track scan results
  struct ScanStatus {
    // If not present, indicates that the scan has not completed yet
    std::optional<wlan_scan_result_t> result_code = std::nullopt;
    std::list<wlanif_bss_description_t> result_list;
  };
  // One entry per scan started
  std::map<uint64_t, ScanStatus> scan_results_;
};

// A base class that can be used for creating simulation tests. It provides functionality that
// should be common to most tests (like creating a new device instance and setting up and plugging
// into the environment). It also provides a factory method for creating a new interface on the
// simulated device.
class SimTest : public ::testing::Test, public simulation::StationIfc {
 public:
  SimTest();
  ~SimTest();

  // In some cases (like error injection that affects the initialization) we want to work with
  // an uninitialized device. This method will allocate, but not initialize the device. To complete
  // initialization, the Init() function can be called after PreInit().
  zx_status_t PreInit();

  // Allocate device (if it hasn't already been allocated) and initialize it. This function doesn't
  // require PreInit() to be called first.
  zx_status_t Init();

  std::shared_ptr<simulation::Environment> env_;

 protected:
  // Create a new interface on the simulated device, providing the specified role and function
  // callbacks
  zx_status_t StartInterface(
      wlan_info_mac_role_t role, SimInterface* sim_ifc,
      std::optional<const wlanif_impl_ifc_protocol*> sme_protocol = std::nullopt,
      std::optional<common::MacAddr> mac_addr = std::nullopt);

  // Stop and delete a SimInterface
  void DeleteInterface(uint16_t iface_id);
  void DeleteInterface(const SimInterface& ifc) { DeleteInterface(ifc.iface_id_); }

  // Fake device manager
  std::unique_ptr<simulation::FakeDevMgr> dev_mgr_;

  // brcmfmac's concept of a device
  brcmfmac::SimDevice* device_ = nullptr;

  // Keep track of the ifaces we created during test by iface id.
  std::set<uint16_t> iface_id_set_;

 private:
  // StationIfc methods - by default, do nothing. These can/will be overridden by superclasses.
  void Rx(std::shared_ptr<const simulation::SimFrame> frame,
          std::shared_ptr<const simulation::WlanRxInfo> info) override {}
};

// Schedule a call from within a SimTest to a member function that takes no arguments
#define SCHEDULE_CALL(when, ...)                            \
  do {                                                      \
    auto cb_fn = std::make_unique<std::function<void()>>(); \
    *cb_fn = std::bind(__VA_ARGS__);                        \
    env_->ScheduleNotification(std::move(cb_fn), when);     \
  } while (0)

}  // namespace wlan::brcmfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_TEST_SIM_TEST_H_
