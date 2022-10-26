// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_WLAN_SOFTMAC_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_WLAN_SOFTMAC_DEVICE_H_

#include <fidl/fuchsia.wlan.softmac/cpp/driver/wire.h>
#include <lib/ddk/device.h>
#include <lib/fdf/cpp/dispatcher.h>

#include <memory>

#include <ddktl/device.h>

#include "banjo/ieee80211.h"

struct iwl_mvm_vif;
struct iwl_trans;

namespace wlan::iwlwifi {

class MvmSta;
class WlanSoftmacDevice;

class WlanSoftmacDevice : public ddk::Device<WlanSoftmacDevice, ddk::Initializable, ddk::Unbindable,
                                             ddk::ServiceConnectable>,
                          public fdf::WireServer<fuchsia_wlan_softmac::WlanSoftmac> {
 public:
  WlanSoftmacDevice(zx_device* parent, iwl_trans* drvdata, uint16_t iface_id,
                    struct iwl_mvm_vif* mvmvif);
  ~WlanSoftmacDevice();

  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);
  zx_status_t DdkServiceConnect(const char* service_name, fdf::Channel channel);

  // WlanSoftmac protocol implementation.
  void Query(fdf::Arena& arena, QueryCompleter::Sync& completer);
  void QueryDiscoverySupport(fdf::Arena& arena, QueryDiscoverySupportCompleter::Sync& completer);
  void QueryMacSublayerSupport(fdf::Arena& arena,
                               QueryMacSublayerSupportCompleter::Sync& completer);
  void QuerySecuritySupport(fdf::Arena& arena, QuerySecuritySupportCompleter::Sync& completer);
  void QuerySpectrumManagementSupport(fdf::Arena& arena,
                                      QuerySpectrumManagementSupportCompleter::Sync& completer);
  void Start(StartRequestView request, fdf::Arena& arena, StartCompleter::Sync& completer);
  void Stop(fdf::Arena& arena, StopCompleter::Sync& completer);
  void QueueTx(QueueTxRequestView request, fdf::Arena& arena, QueueTxCompleter::Sync& completer);
  void SetChannel(SetChannelRequestView request, fdf::Arena& arena,
                  SetChannelCompleter::Sync& completer);
  void ConfigureBss(ConfigureBssRequestView request, fdf::Arena& arena,
                    ConfigureBssCompleter::Sync& completer);
  void EnableBeaconing(EnableBeaconingRequestView request, fdf::Arena& arena,
                       EnableBeaconingCompleter::Sync& completer);
  void ConfigureBeacon(ConfigureBeaconRequestView request, fdf::Arena& arena,
                       ConfigureBeaconCompleter::Sync& completer);
  void SetKey(SetKeyRequestView request, fdf::Arena& arena, SetKeyCompleter::Sync& completer);
  void ConfigureAssoc(ConfigureAssocRequestView request, fdf::Arena& arena,
                      ConfigureAssocCompleter::Sync& completer);
  void ClearAssoc(ClearAssocRequestView request, fdf::Arena& arena,
                  ClearAssocCompleter::Sync& completer);
  void StartPassiveScan(StartPassiveScanRequestView request, fdf::Arena& arena,
                        StartPassiveScanCompleter::Sync& completer);
  void StartActiveScan(StartActiveScanRequestView request, fdf::Arena& arena,
                       StartActiveScanCompleter::Sync& completer);
  void CancelScan(CancelScanRequestView request, fdf::Arena& arena,
                  CancelScanCompleter::Sync& completer);
  void UpdateWmmParams(UpdateWmmParamsRequestView request, fdf::Arena& arena,
                       UpdateWmmParamsCompleter::Sync& completer);

  // Entry functions to access WlanSoftmacIfc protocol implementation in client_.
  void Recv(fuchsia_wlan_softmac::wire::WlanRxPacket* rx_packet);
  void ScanComplete(zx_status_t status, uint64_t scan_id);

  // Helper function
  bool IsValidChannel(const fuchsia_wlan_common::wire::WlanChannel* channel);

  // Exposing for tests, initializing server end dispatcher for WlanSoftmacIfc protocol.
  zx_status_t InitServerDispatcher();
  // Exposing for tests, initializing server end dispatcher for WlanSoftmac protocol.
  zx_status_t InitClientDispatcher();

 protected:
  struct iwl_mvm_vif* mvmvif_;

 private:
  iwl_trans* drvdata_;
  uint16_t iface_id_;

  // True if the mac_start() has been executed successfully.
  bool mac_started;

  // Each peer on this interface will require a MvmSta instance.  For now, as we only support client
  // mode, we have only one peer (the AP), which simplifies things.
  std::unique_ptr<MvmSta> ap_mvm_sta_;

  // Dispatcher for FIDL client of WlanSoftmacIfc protocol.
  fdf::Dispatcher client_dispatcher_;

  // Dispatcher for FIDL server of WlanSoftmac protocol.
  fdf::Dispatcher server_dispatcher_;

  // The FIDL client to communicate with Wlan device.
  fdf::WireSharedClient<fuchsia_wlan_softmac::WlanSoftmacIfc> client_;

  // Store unbind txn for async reply.
  std::optional<::ddk::UnbindTxn> unbind_txn_;
};

}  // namespace wlan::iwlwifi

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_WLAN_SOFTMAC_DEVICE_H_
