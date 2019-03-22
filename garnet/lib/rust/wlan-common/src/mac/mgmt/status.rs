// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use num_derive::{FromPrimitive, ToPrimitive};

/// IEEE Std 802.11-2016, 9.4.1.9, Table 9-46
#[allow(unused)] // Some StatusCodes are not used yet.
#[derive(Debug, PartialOrd, PartialEq, FromPrimitive, ToPrimitive)]
#[repr(C)]
pub enum StatusCode {
    Success = 0,
    Refused = 1,
    TdlsRejectedAlternativeProvided = 2,
    TdlsRejected = 3,
    // 4 Reserved
    SecurityDisabled = 5,
    UnacceptableLifetime = 6,
    NotInSameBss = 7,
    // 8-9 Reserved
    RefusedCapabilitiesMismatch = 10,
    DeniedNoAssociationExists = 11,
    DeniedOtherReason = 12,
    UnsupportedAuthAlgorithm = 13,
    TransactionSequenceError = 14,
    ChallengeFailure = 15,
    RejectedSequenceTimeout = 16,
    DeniedNoMoreStas = 17,
    RefusedBasicRatesMismatch = 18,
    DeniedNoShortPreambleSupport = 19,
    // 20-21 Reserved
    RejectedSpectrumManagementRequired = 22,
    RejectedBadPowerCapability = 23,
    RejectedBadSupportedChannels = 24,
    DeniedNoShortSlotTimeSupport = 25,
    // 26 Reserved
    DeniedNoHtSupport = 27,
    R0khUnreachable = 28,
    DeniedPcoTimeNotSupported = 29,
    RefusedTemporarily = 30,
    RobustManagementPolicyViolation = 31,
    UnspecifiedQosFailure = 32,
    DeniedInsufficientBandwidth = 33,
    DeniedPoorChannelConditions = 34,
    DeniedQosNotSupported = 35,
    // 36 Reserved
    RequestDeclined = 37,
    InvalidParameters = 38,
    RejectedWithSuggestedChanges = 39,
    StatusInvalidElement = 40,
    StatusInvalidGroupCipher = 41,
    StatusInvalidPairwiseCipher = 42,
    StatusInvalidAkmp = 43,
    UnsupportedRsneVersion = 44,
    InvalidRsneCapabilities = 45,
    StatusCipherOutOfPolicy = 46,
    RejectedForDelayPeriod = 47,
    DlsNotAllowed = 48,
    NotPresent = 49,
    NotQosSta = 50,
    DeniedListenIntervalTooLarge = 51,
    StatusInvalidFtActionFrameCount = 52,
    StatusInvalidPmkid = 53,
    StatusInvalidMde = 54,
    StatusInvalidFte = 55,
    RequestedTclasNotSupported56 = 56, // see RequestedTclasNotSupported80 below
    InsufficientTclasProcessingResources = 57,
    TryAnotherBss = 58,
    GasAdvertisementProtocolNotSupported = 59,
    NoOutstandingGasRequest = 60,
    GasResponseNotReceivedFromServer = 61,
    GasQueryTimeout = 62,
    GasQueryResponseTooLarge = 63,
    RejectedHomeWithSuggestedChanges = 64,
    ServerUnreachable = 65,
    // 66 Reserved
    RejectedForSspPermissions = 67,
    RefusedUnauthenticatedAccessNotSupported = 68,
    // 69-71 Reserved
    InvalidRsne = 72,
    UApsdCoexistanceNotSupported = 73,
    UApsdCoexModeNotSupported = 74,
    BadIntervalWithUApsdCoex = 75,
    AntiCloggingTokenRequired = 76,
    UnsupportedFiniteCyclicGroup = 77,
    CannotFindAlternativeTbtt = 78,
    TransmissionFailure = 79,
    RequestedTclasNotSupported80 = 80, // see RequestedTclasNotSupported56 above
    TclasResourcesExhausted = 81,
    RejectedWithSuggestedBssTransition = 82,
    RejectWithSchedule = 83,
    RejectNoWakeupSpecified = 84,
    SuccessPowerSaveMode = 85,
    PendingAdmittingFstSession = 86,
    PerformingFstNow = 87,
    PendingGapInBaWindow = 88,
    RejectUPidSetting = 89,
    // 90-91 Reserved
    RefusedExternalReason = 92,
    RefusedApOutOfMemory = 93,
    RejectedEmergencyServicesNotSupported = 94,
    QueryResponseOutstanding = 95,
    RejectDseBand = 96,
    TclasProcessingTerminated = 97,
    TsScheduleConflict = 98,
    DeniedWithSuggestedBandAndChannel = 99,
    MccaopReservationConflict = 100,
    MafLimitExceeded = 101,
    MccaTrackLimitExceeded = 102,
    DeniedDueToSpectrumManagement = 103,
    DeniedVhtNotSupported = 104,
    EnablementDenied = 105,
    RestrictionFromAuthorizedGdb = 106,
    AuthorizationDeenabled = 107,
    // 108-65535 Reserved
}
