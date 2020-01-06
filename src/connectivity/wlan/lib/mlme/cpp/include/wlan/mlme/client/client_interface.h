// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_CLIENT_CLIENT_INTERFACE_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_CLIENT_CLIENT_INTERFACE_H_

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <fuchsia/wlan/stats/cpp/fidl.h>
#include <zircon/types.h>

#include <memory>

#include <wlan/common/macaddr.h>
#include <wlan/mlme/client/timeout_target.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/timer_manager.h>

namespace wlan {

class ClientInterface {
 public:
  ClientInterface(ClientInterface const&) = delete;
  ClientInterface& operator=(ClientInterface const&) = delete;
  virtual ~ClientInterface() = default;

  virtual zx_status_t HandleEthFrame(EthFrame&&) = 0;
  virtual zx_status_t HandleWlanFrame(std::unique_ptr<Packet>) = 0;
  virtual void HandleTimeout(zx::time now, TimeoutTarget target, TimeoutId timeout_id) = 0;
  virtual zx_status_t Authenticate(::fuchsia::wlan::mlme::AuthenticationTypes auth_type,
                                   uint32_t timeout) = 0;
  virtual zx_status_t Deauthenticate(::fuchsia::wlan::mlme::ReasonCode reason_code) = 0;
  virtual zx_status_t Associate(const ::fuchsia::wlan::mlme::AssociateRequest& req) = 0;
  virtual zx_status_t SendEapolFrame(fbl::Span<const uint8_t> eapol_frame,
                                     const common::MacAddr& src, const common::MacAddr& dst) = 0;
  virtual zx_status_t SetKeys(fbl::Span<const ::fuchsia::wlan::mlme::SetKeyDescriptor> keys) = 0;
  virtual void UpdateControlledPort(::fuchsia::wlan::mlme::ControlledPortState state) = 0;

  virtual ::fuchsia::wlan::stats::ClientMlmeStats stats() const = 0;
  virtual void ResetStats() = 0;

  virtual wlan_client_sta_t* GetRustClientSta() = 0;
  virtual void NotifyAutoDeauth() = 0;

 protected:
  ClientInterface() = default;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_CLIENT_CLIENT_INTERFACE_H_
