// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_CLIENT_CLIENT_INTERFACE_H_
#define GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_CLIENT_CLIENT_INTERFACE_H_

#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <fuchsia/wlan/stats/cpp/fidl.h>

#include <fbl/unique_ptr.h>
#include <wlan/common/macaddr.h>
#include <zircon/types.h>

namespace wlan {

class ClientInterface {
   public:
    ClientInterface(ClientInterface const&) = delete;
    ClientInterface& operator=(ClientInterface const&) = delete;
    virtual ~ClientInterface() = default;

    virtual zx_status_t HandleEthFrame(EthFrame&&) = 0;
    virtual zx_status_t HandleWlanFrame(fbl::unique_ptr<Packet>) = 0;
    virtual zx_status_t HandleTimeout() = 0;
    virtual zx_status_t Authenticate(::fuchsia::wlan::mlme::AuthenticationTypes auth_type,
                                     uint32_t timeout) = 0;
    virtual zx_status_t Deauthenticate(::fuchsia::wlan::mlme::ReasonCode reason_code) = 0;
    virtual zx_status_t Associate(Span<const uint8_t> rsne) = 0;
    virtual zx_status_t SendEapolFrame(Span<const uint8_t> eapol_frame, const common::MacAddr& src,
                                       const common::MacAddr& dst) = 0;
    virtual zx_status_t SetKeys(Span<const ::fuchsia::wlan::mlme::SetKeyDescriptor> keys) = 0;
    virtual void UpdateControlledPort(::fuchsia::wlan::mlme::ControlledPortState state) = 0;

    virtual void PreSwitchOffChannel() = 0;
    virtual void BackToMainChannel() = 0;

    virtual ::fuchsia::wlan::stats::ClientMlmeStats stats() const = 0;
    virtual void ResetStats() = 0;

   protected:
    ClientInterface() = default;
};

}  // namespace wlan

#endif  // GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_CLIENT_CLIENT_INTERFACE_H_
