// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use num_derive::{FromPrimitive, ToPrimitive};

/// IEEE Std 802.11-2016, 9.4.1.7
#[allow(unused)] // Some ReasonCodes are not used yet.
#[derive(Debug, PartialOrd, PartialEq, FromPrimitive, ToPrimitive)]
#[repr(C)]
pub enum ReasonCode {
    // 0 Reserved
    UnspecifiedReason = 1,
    InvalidAuthentication = 2,
    LeavingNetworkDeauth = 3,
    ReasonInactivity = 4,
    NoMoreStas = 5,
    InvalidClass2Frame = 6,
    InvalidClass3Frame = 7,
    LeavingNetworkDisassoc = 8,
    NotAuthenticated = 9,
    UnacceptablePowerCapability = 10,
    UnacceptableSupportedChannels = 11,
    BssTransitionDisassoc = 12,
    ReasonInvalidElement = 13,
    MicFailure = 14,
    FourwayHandshakeTimeout = 15,
    GkHandshakeTimeout = 16,
    HandshakeElementMismatch = 17,
    ReasonInvalidGroupCipher = 18,
    ReasonInvalidPairwiseCipher = 19,
    ReasonInvalidAkmp = 20,
    UnsupportedRsneVersion = 21,
    InvalidRsneCapabilities = 22,
    Ieee8021XAuthFailed = 23,
    ReasonCipherOutOfPolicy = 24,
    TdlsPeerUnreachable = 25,
    TdlsUnspecifiedReason = 26,
    SspRequestedDisassoc = 27,
    NoSspRoamingAgreement = 28,
    BadCipherOrAkm = 29,
    NotAuthorizedThisLocation = 30,
    ServiceChangePrecludesTs = 31,
    UnspecifiedQosReason = 32,
    NotEnoughBandwidth = 33,
    MissingAcks = 34,
    ExceededTxop = 35,
    StaLeaving = 36,
    EndTsBaDls = 37,
    UnknownTsBa = 38,
    Timeout = 39,
    // 40 - 44 Reserved.
    PeerkeyMismatch = 45,
    PeerInitiated = 46,
    ApInitiated = 47,
    ReasonInvalidFtActionFrameCount = 48,
    ReasonInvalidPmkid = 49,
    ReasonInvalidMde = 50,
    ReasonInvalidFte = 51,
    MeshPeeringCanceled = 52,
    MeshMaxPeers = 53,
    MeshConfigurationPolicyViolation = 54,
    MeshCloseRcvd = 55,
    MeshMaxRetries = 56,
    MeshConfirmTimeout = 57,
    MeshInvalidGtk = 58,
    MeshInconsistentParameters = 59,
    MeshInvalidSecurityCapability = 60,
    MeshPathErrorNoProxyInformation = 61,
    MeshPathErrorNoForwardingInformation = 62,
    MeshPathErrorDestinationUnreachable = 63,
    MacAddressAlreadyExistsInMbss = 64,
    MeshChannelSwitchRegulatoryRequirements = 65,
    MeshChannelSwitchUnspecified = 66,
    // 67-65535 Reserved
}
