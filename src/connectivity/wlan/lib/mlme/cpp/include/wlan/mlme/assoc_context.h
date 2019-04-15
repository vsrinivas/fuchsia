// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_ASSOC_CONTEXT_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_ASSOC_CONTEXT_H_

#include <wlan/common/element.h>
#include <wlan/common/span.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/protocol/mac.h>

#include <optional>
#include <vector>

namespace wlan {
// Information defined only within a context of association
// Beware the subtle interpretation of each field: they are designed to
// reflect the parameters safe to use within an association
// Many parameters do not distinguish Rx capability from Tx capability.
// In those cases, a capability is commonly applied to both Rx and Tx.
// Some parameters are distinctively for Rx only, and some are Tx only.
struct AssocContext {
  // TODO(porce): Move association-related variables of class Station to here
  zx::time ts_start;  // timestamp of the beginning of the association

  // BSSID of the association.
  // Not necessarily the same as the BSSID that is used outside this context.
  // E.g., during joining, authenticating, asssociating, off-channel scanning.
  common::MacAddr bssid;

  CapabilityInfo cap;
  uint16_t aid = 0;
  uint16_t listen_interval = 0;

  // Negotiated configurations
  // This is an outcome of intersection of capabilities and configurations.

  // Concatenation of SupportedRates and ExtendedSupportedRates
  std::vector<SupportedRate> rates;

  // Rx MCS Bitmask in Supported MCS Set field represents the set of MCS
  // the peer can receive at from this device, considering this device's Tx
  // capability.
  std::optional<HtCapabilities> ht_cap = std::nullopt;
  std::optional<HtOperation> ht_op = std::nullopt;
  std::optional<VhtCapabilities> vht_cap = std::nullopt;
  std::optional<VhtOperation> vht_op = std::nullopt;

  PHY phy = WLAN_PHY_OFDM;
  wlan_channel_t chan;

  bool is_cbw40_rx = false;
  bool is_cbw40_tx = false;

  PHY DerivePhy() const;
  wlan_assoc_ctx_t ToDdk() const;
};

const wlan_band_info_t* FindBand(const wlan_info_t& ifc_info, bool is_5ghz);

std::optional<std::vector<SupportedRate>> BuildAssocReqSuppRates(
    const std::vector<uint8_t>& ap_basic_rate_set,
    const std::vector<uint8_t>& ap_op_rate_set,
    const std::vector<SupportedRate>& client_rates);

// Visable only for unit testing.
std::optional<AssocContext> ParseAssocRespIe(Span<const uint8_t> ie_chains);

AssocContext MakeClientAssocCtx(const wlan_info_t& ifc_info,
                                const wlan_channel_t join_chan);
std::optional<AssocContext> MakeBssAssocCtx(
    const AssociationResponse& assoc_resp, Span<const uint8_t> ie_chains,
    const common::MacAddr& peer);

AssocContext IntersectAssocCtx(const AssocContext& bss,
                               const AssocContext& client);

}  // namespace wlan
#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_ASSOC_CONTEXT_H_
