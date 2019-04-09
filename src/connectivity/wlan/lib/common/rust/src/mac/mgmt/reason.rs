// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::{AsBytes, FromBytes};

#[repr(C)]
#[derive(AsBytes, FromBytes, PartialEq, Eq, Clone, Copy, Debug, Default)]
pub struct ReasonCode(pub u16);

/// IEEE Std 802.11-2016, 9.4.1.7
impl ReasonCode {
    // 0 Reserved
    pub const UNSPECIFIED_REASON: Self = Self(1);
    pub const INVALID_AUTHENTICATION: Self = Self(2);
    pub const LEAVING_NETWORK_DEAUTH: Self = Self(3);
    pub const REASON_INACTIVITY: Self = Self(4);
    pub const NO_MORE_STAS: Self = Self(5);
    pub const INVALID_CLASS2FRAME: Self = Self(6);
    pub const INVALID_CLASS3FRAME: Self = Self(7);
    pub const LEAVING_NETWORK_DISASSOC: Self = Self(8);
    pub const NOT_AUTHENTICATED: Self = Self(9);
    pub const UNACCEPTABLE_POWER_CAPABILITY: Self = Self(10);
    pub const UNACCEPTABLE_SUPPORTED_CHANNELS: Self = Self(11);
    pub const BSS_TRANSITION_DISASSOC: Self = Self(12);
    pub const REASON_INVALID_ELEMENT: Self = Self(13);
    pub const MIC_FAILURE: Self = Self(14);
    pub const FOURWAY_HANDSHAKE_TIMEOUT: Self = Self(15);
    pub const GK_HANDSHAKE_TIMEOUT: Self = Self(16);
    pub const HANDSHAKE_ELEMENT_MISMATCH: Self = Self(17);
    pub const REASON_INVALID_GROUP_CIPHER: Self = Self(18);
    pub const REASON_INVALID_PAIRWISE_CIPHER: Self = Self(19);
    pub const REASON_INVALID_AKMP: Self = Self(20);
    pub const UNSUPPORTED_RSNE_VERSION: Self = Self(21);
    pub const INVALID_RSNE_CAPABILITIES: Self = Self(22);
    pub const IEEE802_1_X_AUTH_FAILED: Self = Self(23);
    pub const REASON_CIPHER_OUT_OF_POLICY: Self = Self(24);
    pub const TDLS_PEER_UNREACHABLE: Self = Self(25);
    pub const TDLS_UNSPECIFIED_REASON: Self = Self(26);
    pub const SSP_REQUESTED_DISASSOC: Self = Self(27);
    pub const NO_SSP_ROAMING_AGREEMENT: Self = Self(28);
    pub const BAD_CIPHER_OR_AKM: Self = Self(29);
    pub const NOT_AUTHORIZED_THIS_LOCATION: Self = Self(30);
    pub const SERVICE_CHANGE_PRECLUDES_TS: Self = Self(31);
    pub const UNSPECIFIED_QOS_REASON: Self = Self(32);
    pub const NOT_ENOUGH_BANDWIDTH: Self = Self(33);
    pub const MISSING_ACKS: Self = Self(34);
    pub const EXCEEDED_TXOP: Self = Self(35);
    pub const STA_LEAVING: Self = Self(36);
    pub const END_TS_BA_DLS: Self = Self(37);
    pub const UNKNOWN_TS_BA: Self = Self(38);
    pub const TIMEOUT: Self = Self(39);
    // 40 - 44 Reserved.
    pub const PEERKEY_MISMATCH: Self = Self(45);
    pub const PEER_INITIATED: Self = Self(46);
    pub const AP_INITIATED: Self = Self(47);
    pub const REASON_INVALID_FT_ACTION_FRAME_COUNT: Self = Self(48);
    pub const REASON_INVALID_PMKID: Self = Self(49);
    pub const REASON_INVALID_MDE: Self = Self(50);
    pub const REASON_INVALID_FTE: Self = Self(51);
    pub const MESH_PEERING_CANCELED: Self = Self(52);
    pub const MESH_MAX_PEERS: Self = Self(53);
    pub const MESH_CONFIGURATION_POLICY_VIOLATION: Self = Self(54);
    pub const MESH_CLOSE_RCVD: Self = Self(55);
    pub const MESH_MAX_RETRIES: Self = Self(56);
    pub const MESH_CONFIRM_TIMEOUT: Self = Self(57);
    pub const MESH_INVALID_GTK: Self = Self(58);
    pub const MESH_INCONSISTENT_PARAMETERS: Self = Self(59);
    pub const MESH_INVALID_SECURITY_CAPABILITY: Self = Self(60);
    pub const MESH_PATH_ERROR_NO_PROXY_INFORMATION: Self = Self(61);
    pub const MESH_PATH_ERROR_NO_FORWARDING_INFORMATION: Self = Self(62);
    pub const MESH_PATH_ERROR_DESTINATION_UNREACHABLE: Self = Self(63);
    pub const MAC_ADDRESS_ALREADY_EXISTS_IN_MBSS: Self = Self(64);
    pub const MESH_CHANNEL_SWITCH_REGULATORY_REQUIREMENTS: Self = Self(65);
    pub const MESH_CHANNEL_SWITCH_UNSPECIFIED: Self = Self(66);
    // 67-65535 Reserved
}
