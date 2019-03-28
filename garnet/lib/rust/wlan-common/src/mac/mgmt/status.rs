// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::{AsBytes, FromBytes};

#[repr(C)]
#[derive(AsBytes, FromBytes, PartialEq, Eq, Clone, Copy, Debug, Default)]
pub struct StatusCode(pub u16);

/// IEEE Std 802.11-2016, 9.4.1.9, Table 9-46
impl StatusCode {
    pub const SUCCESS: Self = Self(0);
    pub const REFUSED: Self = Self(1);
    pub const TDLS_REJECTED_ALTERNATIVE_PROVIDED: Self = Self(2);
    pub const TDLS_REJECTED: Self = Self(3);
    // 4 Reserved
    pub const SECURITY_DISABLED: Self = Self(5);
    pub const UNACCEPTABLE_LIFETIME: Self = Self(6);
    pub const NOT_IN_SAME_BSS: Self = Self(7);
    // 8-9 Reserved
    pub const REFUSED_CAPABILITIES_MISMATCH: Self = Self(10);
    pub const DENIED_NO_ASSOCIATION_EXISTS: Self = Self(11);
    pub const DENIED_OTHER_REASON: Self = Self(12);
    pub const UNSUPPORTED_AUTH_ALGORITHM: Self = Self(13);
    pub const TRANSACTION_SEQUENCE_ERROR: Self = Self(14);
    pub const CHALLENGE_FAILURE: Self = Self(15);
    pub const REJECTED_SEQUENCE_TIMEOUT: Self = Self(16);
    pub const DENIED_NO_MORE_STAS: Self = Self(17);
    pub const REFUSED_BASIC_RATES_MISMATCH: Self = Self(18);
    pub const DENIED_NO_SHORT_PREAMBLE_SUPPORT: Self = Self(19);
    // 20-21 Reserved
    pub const REJECTED_SPECTRUM_MANAGEMENT_REQUIRED: Self = Self(22);
    pub const REJECTED_BAD_POWER_CAPABILITY: Self = Self(23);
    pub const REJECTED_BAD_SUPPORTED_CHANNELS: Self = Self(24);
    pub const DENIED_NO_SHORT_SLOT_TIME_SUPPORT: Self = Self(25);
    // 26 Reserved
    pub const DENIED_NO_HT_SUPPORT: Self = Self(27);
    pub const R0KH_UNREACHABLE: Self = Self(28);
    pub const DENIED_PCO_TIME_NOT_SUPPORTED: Self = Self(29);
    pub const REFUSED_TEMPORARILY: Self = Self(30);
    pub const ROBUST_MANAGEMENT_POLICY_VIOLATION: Self = Self(31);
    pub const UNSPECIFIED_QOS_FAILURE: Self = Self(32);
    pub const DENIED_INSUFFICIENT_BANDWIDTH: Self = Self(33);
    pub const DENIED_POOR_CHANNEL_CONDITIONS: Self = Self(34);
    pub const DENIED_QOS_NOT_SUPPORTED: Self = Self(35);
    // 36 Reserved
    pub const REQUEST_DECLINED: Self = Self(37);
    pub const INVALID_PARAMETERS: Self = Self(38);
    pub const REJECTED_WITH_SUGGESTED_CHANGES: Self = Self(39);
    pub const STATUS_INVALID_ELEMENT: Self = Self(40);
    pub const STATUS_INVALID_GROUP_CIPHER: Self = Self(41);
    pub const STATUS_INVALID_PAIRWISE_CIPHER: Self = Self(42);
    pub const STATUS_INVALID_AKMP: Self = Self(43);
    pub const UNSUPPORTED_RSNE_VERSION: Self = Self(44);
    pub const INVALID_RSNE_CAPABILITIES: Self = Self(45);
    pub const STATUS_CIPHER_OUT_OF_POLICY: Self = Self(46);
    pub const REJECTED_FOR_DELAY_PERIOD: Self = Self(47);
    pub const DLS_NOT_ALLOWED: Self = Self(48);
    pub const NOT_PRESENT: Self = Self(49);
    pub const NOT_QOS_STA: Self = Self(50);
    pub const DENIED_LISTEN_INTERVAL_TOO_LARGE: Self = Self(51);
    pub const STATUS_INVALID_FT_ACTION_FRAME_COUNT: Self = Self(52);
    pub const STATUS_INVALID_PMKID: Self = Self(53);
    pub const STATUS_INVALID_MDE: Self = Self(54);
    pub const STATUS_INVALID_FTE: Self = Self(55);
    // See also REQUESTED_TCLAS_NOT_SUPPORTED80 below
    pub const REQUESTED_TCLAS_NOT_SUPPORTED56: Self = Self(56);
    pub const INSUFFICIENT_TCLAS_PROCESSING_RESOURCES: Self = Self(57);
    pub const TRY_ANOTHER_BSS: Self = Self(58);
    pub const GAS_ADVERTISEMENT_PROTOCOL_NOT_SUPPORTED: Self = Self(59);
    pub const NO_OUTSTANDING_GAS_REQUEST: Self = Self(60);
    pub const GAS_RESPONSE_NOT_RECEIVED_FROM_SERVER: Self = Self(61);
    pub const GAS_QUERY_TIMEOUT: Self = Self(62);
    pub const GAS_QUERY_RESPONSE_TOO_LARGE: Self = Self(63);
    pub const REJECTED_HOME_WITH_SUGGESTED_CHANGES: Self = Self(64);
    pub const SERVER_UNREACHABLE: Self = Self(65);
    // 66 Reserved
    pub const REJECTED_FOR_SSP_PERMISSIONS: Self = Self(67);
    pub const REFUSED_UNAUTHENTICATED_ACCESS_NOT_SUPPORTED: Self = Self(68);
    // 69-71 Reserved
    pub const INVALID_RSNE: Self = Self(72);
    pub const UAPSD_COEXISTANCE_NOT_SUPPORTED: Self = Self(73);
    pub const UAPSD_COEX_MODE_NOT_SUPPORTED: Self = Self(74);
    pub const BAD_INTERVAL_WITH_UAPSD_COEX: Self = Self(75);
    pub const ANTI_CLOGGING_TOKEN_REQUIRED: Self = Self(76);
    pub const UNSUPPORTED_FINITE_CYCLIC_GROUP: Self = Self(77);
    pub const CANNOT_FIND_ALTERNATIVE_TBTT: Self = Self(78);
    pub const TRANSMISSION_FAILURE: Self = Self(79);
    // See also REQUESTED_TCLAS_NOT_SUPPORTED56 above
    pub const REQUESTED_TCLAS_NOT_SUPPORTED80: Self = Self(80);
    pub const TCLAS_RESOURCES_EXHAUSTED: Self = Self(81);
    pub const REJECTED_WITH_SUGGESTED_BSS_TRANSITION: Self = Self(82);
    pub const REJECT_WITH_SCHEDULE: Self = Self(83);
    pub const REJECT_NO_WAKEUP_SPECIFIED: Self = Self(84);
    pub const SUCCESS_POWER_SAVE_MODE: Self = Self(85);
    pub const PENDING_ADMITTING_FST_SESSION: Self = Self(86);
    pub const PERFORMING_FST_NOW: Self = Self(87);
    pub const PENDING_GAP_IN_BA_WINDOW: Self = Self(88);
    pub const REJECT_UPID_SETTING: Self = Self(89);
    // 90-91 Reserved
    pub const REFUSED_EXTERNAL_REASON: Self = Self(92);
    pub const REFUSED_AP_OUT_OF_MEMORY: Self = Self(93);
    pub const REJECTED_EMERGENCY_SERVICES_NOT_SUPPORTED: Self = Self(94);
    pub const QUERY_RESPONSE_OUTSTANDING: Self = Self(95);
    pub const REJECT_DSE_BAND: Self = Self(96);
    pub const TCLAS_PROCESSING_TERMINATED: Self = Self(97);
    pub const TS_SCHEDULE_CONFLICT: Self = Self(98);
    pub const DENIED_WITH_SUGGESTED_BAND_AND_CHANNEL: Self = Self(99);
    pub const MCCAOP_RESERVATION_CONFLICT: Self = Self(100);
    pub const MAF_LIMIT_EXCEEDED: Self = Self(101);
    pub const MCCA_TRACK_LIMIT_EXCEEDED: Self = Self(102);
    pub const DENIED_DUE_TO_SPECTRUM_MANAGEMENT: Self = Self(103);
    pub const DENIED_VHT_NOT_SUPPORTED: Self = Self(104);
    pub const ENABLEMENT_DENIED: Self = Self(105);
    pub const RESTRICTION_FROM_AUTHORIZED_GDB: Self = Self(106);
    pub const AUTHORIZATION_DEENABLED: Self = Self(107);
    // 108-65535 Reserved
}
