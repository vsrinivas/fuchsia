// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_AP_BSS_INTERFACE_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_AP_BSS_INTERFACE_H_

#include <fbl/unique_ptr.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/mlme/ap/tim.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/ht.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/ps_cfg.h>
#include <wlan/mlme/timer_manager.h>
#include <zircon/types.h>

#include <chrono>
#include <optional>

namespace wlan {

class Buffer;
class StartRequest;
template <typename T>
class MlmeMsg;

class BssInterface {
 public:
  virtual zx_status_t ScheduleTimeout(wlan_tu_t tus,
                                      const common::MacAddr& client_addr,
                                      TimeoutId* id) = 0;
  virtual void CancelTimeout(TimeoutId id) = 0;
  virtual const common::MacAddr& bssid() const = 0;
  virtual uint64_t timestamp() = 0;

  virtual uint32_t NextSns1(const common::MacAddr& addr) = 0;

  virtual std::optional<DataFrame<LlcHeader>> EthToDataFrame(
      const EthFrame& eth_frame, bool needs_protection) = 0;

  virtual bool IsRsn() const = 0;
  virtual HtConfig Ht() const = 0;
  virtual const Span<const SupportedRate> Rates() const = 0;

  virtual zx_status_t SendMgmtFrame(MgmtFrame<>&& mgmt_frame) = 0;
  virtual zx_status_t SendDataFrame(DataFrame<>&& data_frame,
                                    uint32_t flags = 0) = 0;
  virtual zx_status_t DeliverEthernet(Span<const uint8_t> frame) = 0;

  // Indications reported from lower MAC layer.
  virtual void OnPreTbtt() = 0;
  virtual void OnBcnTxComplete() = 0;

  virtual wlan_channel_t Chan() const = 0;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_AP_BSS_INTERFACE_H_
