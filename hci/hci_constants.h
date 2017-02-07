// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstddef>

// This file contains constants and numbers used in HCI packet payloads.

namespace bluetooth {
namespace hci {

// HCI_Version Assigned Values See the "Assigned Numbers" document for
// reference.
// (https://www.bluetooth.com/specifications/assigned-numbers/host-controller-interface)
enum class HCIVersion {
  // Bluetooth Core Specification 1.0b
  k1_0b = 0,

  // Bluetooth Core Specification 1.1
  k1_1,

  // Bluetooth Core Specification 1.2
  k1_2,

  // Bluetooth Core Specification 2.0 + EDR
  k2_0_EDR,

  // Bluetooth Core Specification 2.1 + EDR
  k2_1_EDR,

  // Bluetooth Core Specification 3.0 + HS
  k3_0_HS,

  // Bluetooth Core Specification 4.0
  k4_0,

  // Bluetooth Core Specification 4.1
  k4_1,

  // Bluetooth Core Specification 4.2
  k4_2,

  // Bluetooth Core Specification 5.0
  k5_0,

  kReserved
};

// HCI Error Codes. Refer to Core Spec v5.0, Vol 2, Part D for definitions and
// descriptions. All enum values are in increasing numerical order, however the
// values are listed below for clarity.
enum Status : uint8_t {
  kSuccess                                      = 0x00,
  kUnknownCommand                               = 0x01,
  kUnknownConnectionId                          = 0x02,
  kHardwareFailure                              = 0x03,
  kPageTimeout                                  = 0x04,
  kAuthenticationFailure                        = 0x05,
  kPinOrKeyMissing                              = 0x06,
  kMemoryCapacityExceeded                       = 0x07,
  kConnectionTimeout                            = 0x08,
  kConnectionLimitExceeded                      = 0x09,
  kSynchronousConnectionLimitExceeded           = 0x0A,
  kACLConnectionAlreadyExists                   = 0x0B,
  kCommandDisallowed                            = 0x0C,
  kConnectionRejectedLimitedResources           = 0x0D,
  kConnectionRejectedSecurity                   = 0x0E,
  kConnectionRejectedBadBdAddr                  = 0x0F,
  kConnectionAcceptTimeoutExceeded              = 0x10,
  kUnsupportedFeatureOrParameter                = 0x11,
  kInvalidHCICommandParameters                  = 0x12,
  kRemoteUserTerminatedConnection               = 0x13,
  kRemoteDeviceTerminatedConnectionLowResources = 0x14,
  kRemoteDeviceTerminatedConnectionPowerOff     = 0x15,
  kConnectionTerminatedByLocalHost              = 0x16,
  kRepeatedAttempts                             = 0x17,
  kPairingNotAllowed                            = 0x18,
  kUnknownLMPPDU                                = 0x19,
  kUnsupportedRemoteFeature                     = 0x1A,
  kSCOOffsetRejected                            = 0x1B,
  kSCOIntervalRejected                          = 0x1C,
  kSCOAirModeRejected                           = 0x1D,
  kInvalidLMPOrLLParameters                     = 0x1E,
  kUnspecifiedError                             = 0x1F,
  kUnsupportedLMPOrLLParameterValue             = 0x20,
  kRoleChangeNotAllowed                         = 0x21,
  kLMPOrLLResponseTimeout                       = 0x22,
  kLMPErrorTransactionCollision                 = 0x23,
  kLMPPDUNotAllowed                             = 0x24,
  kEncryptionModeNotAcceptable                  = 0x25,
  kLinkKeyCannotBeChanged                       = 0x26,
  kRequestedQoSNotSupported                     = 0x27,
  kInstantPassed                                = 0x28,
  kPairingWithUnitKeyNotSupported               = 0x29,
  kDifferentTransactionCollision                = 0x2A,
  kReserved0                                    = 0x2B,
  kQoSUnacceptableParameter                     = 0x2C,
  kQoSRejected                                  = 0x2D,
  kChannelClassificationNotSupported            = 0x2E,
  kInsufficientSecurity                         = 0x2F,
  kParameterOutOfMandatoryRange                 = 0x30,
  kReserved1                                    = 0x31,
  kRoleSwitchPending                            = 0x32,
  kReserved2                                    = 0x33,
  kReservedSlotViolation                        = 0x34,
  kRoleSwitchFailed                             = 0x35,
  kExtendedInquiryResponseTooLarge              = 0x36,
  kSecureSimplePairingNotSupportedByHost        = 0x37,
  kHostBusyPairing                              = 0x38,
  kConnectionRejectedNoSuitableChannelFound     = 0x39,
  kControllerBusy                               = 0x3A,
  kUnacceptableConnectionParameters             = 0x3B,
  kDirectedAdvertisingTimeout                   = 0x3C,
  kConnectionTerminatedMICFailure               = 0x3D,
  kConnectionFailedToBeEstablished              = 0x3E,
  kMACConnectionFailed                          = 0x3F,
  kCoarseClockAdjustmentRejected                = 0x40,

  // 5.0
  kType0SubmapNotDefined                        = 0x41,
  kUnknownAdvertisingIdentifier                 = 0x42,
  kLimitReached                                 = 0x43,
  kOperationCancelledByHost                     = 0x44,
};

// Bitmask values for the 64-octet Supported Commands bit-field. See Core Spec
// v5.0, Volume 2, Part E, Section 6.27 "Supported Commands".
enum class SupportedCommand : uint8_t {
  // Octet 0
  kInquiry                 = (1 << 0),
  kInquiryCancel           = (1 << 1),
  kPeriodicInquiryMode     = (1 << 2),
  kExitPeriodicInquiryMode = (1 << 3),
  kCreateConnection        = (1 << 4),
  kDisconnect              = (1 << 5),
  kAddSCOConnection        = (1 << 6),  // deprecated
  kCreateConnectionCancel  = (1 << 7),

  // Octet 1
  kAcceptConnectionRequest     = (1 << 0),
  kRejectConnectionRequest     = (1 << 1),
  kLinkKeyRequestReply         = (1 << 2),
  kLinkKeyRequestNegativeReply = (1 << 3),
  kPINCodeRequestReply         = (1 << 4),
  kPINCodeRequestNegativeReply = (1 << 5),
  kChangeConnectionPacketType  = (1 << 6),
  kAuthenticationRequested     = (1 << 7),

  // Octet 2
  kSetConnectionEncryption      = (1 << 0),
  kChangeConnectionLinkKey      = (1 << 1),
  kMasterLinkKey                = (1 << 2),
  kRemoteNameRequest            = (1 << 3),
  kRemoteNameRequestCancel      = (1 << 4),
  kReadRemoteSupportedFeatures  = (1 << 5),
  kReadRemoteExtendedFeatures   = (1 << 6),
  kReadRemoteVersionInformation = (1 << 7),

  // Octet 3
  kReadClockOffset = (1 << 0),
  kReadLMPHandle   = (1 << 1),

  // Octet 4
  kHoldMode      = (1 << 1),
  kSniffMode     = (1 << 2),
  kExitSniffMode = (1 << 3),
  kParkState     = (1 << 4),  // Reserved in 5.0
  kExitParkState = (1 << 5),  // Reserved in 5.0
  kQOSSetup      = (1 << 6),
  kRoleDiscovery = (1 << 7),

  // Octet 5
  kSwitchRole                     = (1 << 0),
  kReadLinkPolicySettings         = (1 << 1),
  kWriteLinkPolicySettings        = (1 << 2),
  kReadDefaultLinkPolicySettings  = (1 << 3),
  kWriteDefaultLinkPolicySettings = (1 << 4),
  kFlowSpecification              = (1 << 5),
  kSetEventMask                   = (1 << 6),
  kReset                          = (1 << 7),

  // Octet 6
  kSetEventFilter       = (1 << 0),
  kFlush                = (1 << 1),
  kReadPINType          = (1 << 2),
  kWritePINType         = (1 << 3),
  kCreateNewUnitKey     = (1 << 4),
  kReadStoredLinkKey    = (1 << 5),
  kWriteStoredLinkKey   = (1 << 6),
  kDeletedStoredLinkKey = (1 << 7),

  // Octet 7
  kWriteLocalName                = (1 << 0),
  kReadLocalname                 = (1 << 1),
  kReadConnectionAttemptTimeout  = (1 << 2),
  kWriteConnectionAttemptTimeout = (1 << 3),
  kReadPageTimeout               = (1 << 4),
  kWritePageTimeout              = (1 << 5),
  kReadScanEnable                = (1 << 6),
  kWriteScanEnable               = (1 << 7),

  // Octet 8
  kReadPageScanActivity      = (1 << 0),
  kWritePageScanActivity     = (1 << 1),
  kReadInquiryScanActivity   = (1 << 2),
  kWriteInquiryScanActivity  = (1 << 3),
  kReadAuthenticationEnable  = (1 << 4),
  kWriteAuthenticationEnable = (1 << 5),
  kReadEncryptionMode        = (1 << 6),  // deprecated
  kWriteEncryptionMode       = (1 << 7),  // deprecated

  // Octet 9
  kReadClassOfDevice                = (1 << 0),
  kWriteClassOfDevice               = (1 << 1),
  kReadVoiceSetting                 = (1 << 2),
  kWriteVoiceSetting                = (1 << 3),
  kReadAutomaticFlushTimeout        = (1 << 4),
  kWriteAutomaticFlushTimeout       = (1 << 5),
  kReadNumBroadcastRetransmissions  = (1 << 6),
  kWriteNumBroadcastRetransmissions = (1 << 7),

  // Octet 10
  kReadHoldModeActivity              = (1 << 0),
  kWriteHoldModeActivity             = (1 << 1),
  kReadTransmitPowerLevel            = (1 << 2),
  kReadSynchronousFlowControlEnable  = (1 << 3),
  kWriteSynchronousFlowControlEnable = (1 << 4),
  kSetControllerToHostFlowControl    = (1 << 5),
  kHostBufferSize                    = (1 << 6),
  kHostNumberOfCompletedPackets      = (1 << 7),

  // Octet 11
  kReadLinkSupervisionTimeout        = (1 << 0),
  kWriteLinkSupervisionTimeout       = (1 << 1),
  kReadNumberOfSupportedIAC          = (1 << 2),
  kReadCurrentIACLAP                 = (1 << 3),
  kWriteCurrentIACLAP                = (1 << 4),
  kReadPageScanModePeriod            = (1 << 5),  // deprecated
  kWritePageScanModePeriod           = (1 << 6),  // deprecated
  kReadPageScanMode                  = (1 << 7),  // deprecated

  // Octet 12
  kWritePageScanMode               = (1 << 0),  // deprecated
  kSetAFHHostChannelClassification = (1 << 1),
  kReadInquiryScanType             = (1 << 4),
  kWriteInquiryScanType            = (1 << 5),
  kReadInquiryMode                 = (1 << 6),
  kWriteInquiryMode                = (1 << 7),

  // Octet 13
  kReadPageScanType              = (1 << 0),
  kWritePageScanType             = (1 << 1),
  kReadAFHChannelAssessmentMode  = (1 << 2),
  kWriteAFHChannelAssessmentMode = (1 << 3),

  // Octet 14
  kReadLocalVersionInformation = (1 << 3),
  kReadLocalSupportedFeatures  = (1 << 5),
  kReadLocalExtendedFeatures   = (1 << 6),
  kReadBufferSize              = (1 << 7),

  // Octet 15
  kReadCountryCode           = (1 << 0),  // deprecated
  kReadBDADDR                = (1 << 1),
  kReadFailedContactCounter  = (1 << 2),
  kResetFailedContactCOunter = (1 << 3),
  kReadLinkQuality           = (1 << 4),
  kReadRSSI                  = (1 << 5),
  kReadAFHChannelMap         = (1 << 6),
  kReadClock                 = (1 << 7),

  // Octet 16
  kReadLoopbackMode                   = (1 << 0),
  kWriteLoopbackMode                  = (1 << 1),
  kEnableDeviceUnderTestMode          = (1 << 2),
  kSetupSynchronousConnectionRequest  = (1 << 3),
  kAcceptSynchronousConnectionRequest = (1 << 4),
  kRejectSynchronousConnectionRequest = (1 << 5),

  // Octet 17
  kReadExtendedInquiryResponse  = (1 << 0),
  kWriteExtendedInquiryResponse = (1 << 1),
  kRefreshEncryptionKey         = (1 << 2),
  kSniffSubrating               = (1 << 4),
  kReadSimplePairingMode        = (1 << 5),
  kWriteSimplePairingMode       = (1 << 6),
  kReadLocalOOBData             = (1 << 7),

  // Octet 18
  kReadInquiryResponseTransmitPowerLevel = (1 << 0),
  kWriteInquiryTransmitPowerLevel        = (1 << 1),
  kReadDefaultErroneousDataReporting     = (1 << 2),
  kWriteDefaultErroneousDataReporting    = (1 << 3),
  kIOCapabilityRequestReply              = (1 << 7),

  // Octet 19
  kUserConfirmationRequestReply         = (1 << 0),
  kUserConfirmationRequestNegativeReply = (1 << 1),
  kUserPasskeyRequestReply              = (1 << 2),
  kUserPasskeyRequestNegativeReply      = (1 << 3),
  kRemoteOOBDataRequestReply            = (1 << 4),
  kWriteSimplePairingDebugMode          = (1 << 5),
  kEnhancedFlush                        = (1 << 6),
  kRemoteOOBDataRequestNegativeReply    = (1 << 7),

  // Octet 20
  kSendKeypressNotification         = (1 << 2),
  kIOCapabilityRequestNegativeReply = (1 << 3),
  kReadEncryptionKeySize            = (1 << 4),

  // Octet 21
  kCreatePhysicalLink     = (1 << 0),
  kAcceptPhysicalLink     = (1 << 1),
  kDisconnectPhysicalLink = (1 << 2),
  kCreateLogicalLink      = (1 << 3),
  kAcceptLogicalLink      = (1 << 4),
  kDisconnectLogicalLink  = (1 << 5),
  kLogicalLinkCancel      = (1 << 6),
  kFlowSpecModify         = (1 << 7),

  // Octet 22
  kReadLogicalLinkAcceptTimeout  = (1 << 0),
  kWriteLogicalLinkAcceptTimeout = (1 << 1),
  kSetEventMaskPage2             = (1 << 2),
  kReadLocationData              = (1 << 3),
  kWriteLocationData             = (1 << 4),
  kReadLocalAMPInfo              = (1 << 5),
  kReadLocalAMPASSOC             = (1 << 6),
  kWriteRemoteAMPASSOC           = (1 << 7),

  // Octet 23
  kReadFlowControlMode      = (1 << 0),
  kWriteFlowControlMode     = (1 << 1),
  kReadDataBlockSize        = (1 << 2),
  kEnableAMPReceiverReports = (1 << 5),
  kAMPTestEnd               = (1 << 6),
  kAMPTest                  = (1 << 7),

  // Octet 24
  kReadEnhancedTransmitPowerLevel = (1 << 0),
  kReadBestEffortFlushTimeout     = (1 << 2),
  kWriteBestEffortFlushTimeout    = (1 << 3),
  kShortRangeMode                 = (1 << 4),
  kReadLEHostSupported            = (1 << 5),
  kWriteLEHostSupport             = (1 << 6),

  // Octet 25
  kLESetEventMask                  = (1 << 0),
  kLEReadBufferSize                = (1 << 1),
  kLEReadLocalSupportedFeatures    = (1 << 2),
  kLESetRandomAddress              = (1 << 4),
  kLESetAdvertisingParameters      = (1 << 5),
  kLEReadAdvertisingChannelTXPower = (1 << 6),
  kLESetAdvertisingData            = (1 << 7),

  // Octet 26
  kLESetScanResponseData    = (1 << 0),
  kLESetAdvertiseEnable     = (1 << 1),
  kLESetScanParameters      = (1 << 2),
  kLESetScanEnable          = (1 << 3),
  kLECreateConnection       = (1 << 4),
  kLECreateConnectionCancel = (1 << 5),
  kLEReadWhiteListSize      = (1 << 6),
  kLEClearWhiteList         = (1 << 7),

  // Octet 27
  kLEAddDeviceToWhiteList         = (1 << 0),
  kLERemoveDeviceFromWhiteList    = (1 << 1),
  kLEConnectionUpdate             = (1 << 2),
  kLESetHostChannelClassification = (1 << 3),
  kLEReadChannelMap               = (1 << 4),
  kLEReadRemoteUsedFeatures       = (1 << 5),
  kLEEncrypt                      = (1 << 6),
  kLERand                         = (1 << 7),

  // Octet 28
  kLEStartEncryption                 = (1 << 0),
  kLELongTermKeyRequestReply         = (1 << 1),
  kLELongTermKeyRequestNegativeReply = (1 << 2),
  kLEReadSupportedStates             = (1 << 3),
  kLEReceiverTest                    = (1 << 4),
  kLETransmitterTest                 = (1 << 5),
  kLETestEnd                         = (1 << 6),

  // Octet 29
  kEnhancedSetupSynchronousConnection  = (1 << 3),
  kEnhancedAcceptSynchronousConnection = (1 << 4),
  kReadLocalSupportedCodecs            = (1 << 5),
  kSetMWSChannelParameters             = (1 << 6),
  kSetExternalFrameConfiguration       = (1 << 7),

  // Octet 30
  kSetMWSSignaling                   = (1 << 0),
  kSetMWSTransportLayer              = (1 << 1),
  kSetMWSScanFrequencyTable          = (1 << 2),
  kGetMWSTransportLayerConfiguration = (1 << 3),
  kSetMWSPATTERNConfiguration        = (1 << 4),
  kSetTriggeredClockCapture          = (1 << 5),
  kTruncatedPage                     = (1 << 6),
  kTruncatedPageCancel               = (1 << 7),

  // Octet 31
  kSetConnectionlessSlaveBroadcast        = (1 << 0),
  kSetConnectionlessSlaveBroadcastReceive = (1 << 1),
  kStartSynchronizationTrain              = (1 << 2),
  kReceiveSynchronizationTrain            = (1 << 3),
  kSetReservedLTADDR                      = (1 << 4),
  kDeleteReservedLTADDR                   = (1 << 5),
  kSetConnectionlessSlaveBroadcastData    = (1 << 6),
  kReadSynchronizationTrainParameters     = (1 << 7),

  // Octet 32
  kWriteSynchronizationTrainParameters = (1 << 0),
  kRemoteOOBExtendedDataRequestReply   = (1 << 1),
  kReadSecureConnectionsHostSupport    = (1 << 2),
  kWriteSecureConnectionsHostSupport   = (1 << 3),
  kReadAuthenticatedPayloadTimeout     = (1 << 4),
  kWriteAuthenticatedPayloadTimeout    = (1 << 5),
  kReadLocalOOBExtendedData            = (1 << 6),
  kWriteSecureConnectionsTestMode      = (1 << 7),

  // Octet 33
  kReadExtendedPageTimeout                         = (1 << 0),
  kWriteExtendedPageTimeout                        = (1 << 1),
  kReadExtendedInquiryLength                       = (1 << 2),
  kWriteExtendedInquiryLength                      = (1 << 3),
  kLERemoteConnectionParameterRequestReply         = (1 << 4),
  kLERemoteConnectionParameterRequestNegativeReply = (1 << 5),
  kLESetDataLength                                 = (1 << 6),
  kLEReadSuggestedDefaultDataLength                = (1 << 7),

  // Octet 34
  kLEWriteSuggestedDefaultDataLength = (1 << 0),
  kLEReadLocalP256PublicKey          = (1 << 1),
  kLEGenerateDHKey                   = (1 << 2),
  kLEAddDeviceToResolvingList        = (1 << 3),
  kLERemoveDeviceFromResolvingList   = (1 << 4),
  kLEClearResolvingList              = (1 << 5),
  kLEReadResolvingListSize           = (1 << 6),
  kLEReadPeerResolvableAddress       = (1 << 7),

  // Octet 35
  kLEReadLocalResolvableAddress         = (1 << 0),
  kLESetAddressResolutionEnable         = (1 << 1),
  kLESetResolvablePrivateAddressTimeout = (1 << 2),
  kLEReadMaximumDataLength              = (1 << 3),

  // Added in 5.0 (octet 35 cont'd)
  kLEReadPHYCommand                     = (1 << 4),
  kLESetDefaultPHYCommand               = (1 << 5),
  kLESetPHYCommand                      = (1 << 6),
  kLEEnhancedReceiverTestCommand        = (1 << 7),

  // Octet 36
  kLEEnhancedTransmitterTestCommand              = (1 << 0),
  kLESetAdvertisingSetRandomAddressCommand       = (1 << 1),
  kLESetExtendedAdvertisingParametersCommand     = (1 << 2),
  kLESetExtendedAdvertisingDataCommand           = (1 << 3),
  kLESetExtendedScanResponseDataCommand          = (1 << 4),
  kLESetExtendedAdvertisingEnableCommand         = (1 << 5),
  kLEReadMaximumAdvertisingDataLengthCommand     = (1 << 6),
  kLEReadNumberOfSupportedAdvertisingSetsCommand = (1 << 7),

  // Octet 37
  kLERemoveAdvertisingSetCommand             = (1 << 0),
  kLEClearAdvertisingSetsCommand             = (1 << 1),
  kLESetPeriodicAdvertisingParametersCommand = (1 << 2),
  kLESetPeriodicAdvertisingDataCommand       = (1 << 3),
  kLESetPeriodicAdvertisingEnableCommand     = (1 << 4),
  kLESetExtendedScanParametersCommand        = (1 << 5),
  kLESetExtendedScanEnableCommand            = (1 << 6),
  kLEExtendedCreateConnectionCommand         = (1 << 7),

  // Octet 38
  kLEPeriodicAdvertisingCreateSyncCommand          = (1 << 0),
  kLEPeriodicAdvertisingCreateSyncCancelCommand    = (1 << 1),
  kLEPeriodicAdvertisingTerminateSyncCommand       = (1 << 2),
  kLEAddDeviceToPeriodicAdvertiserListCommand      = (1 << 3),
  kLERemoveDeviceFromPeriodicAdvertiserListCommand = (1 << 4),
  kLEClearPeriodicAdvertiserListCommand            = (1 << 5),
  kLEReadPeriodicAdvertiserListSizeCommand         = (1 << 6),
  kLEReadTransmitPowerCommand                      = (1 << 7),

  // Octet 39
  kLEReadRFPathCompensationCommand   = (1 << 0),
  kLEWriteRFPathCompensationCommand  = (1 << 1),
  kLESetPrivacyMode                  = (1 << 2),
};

// Bitmask values for the 8-octet LE Supported Features bit-field. See Core Spec
// v5.0, Volume 6, Part B, Section 4.6 "Feature Support".
enum class LESupportedFeature : uint8_t {
  // Octet 0
  kLEEncryption                         = (1 << 0),
  kConnectionParametersRequestProcedure = (1 << 1),
  kExtendedRejectIndication             = (1 << 2),
  kSlaveInitiatedFeaturesExchange       = (1 << 3),
  kLEPing                               = (1 << 4),
  kLEDataPacketLengthExtension          = (1 << 5),
  kLLPrivacy                            = (1 << 6),
  kExtendedScannerFilterPolicies        = (1 << 7),

  // Added in 5.0
  // Octet 1
  kStableModulationIndexTransmitter     = (1 << 0),
  kStableModulationIndexReceiver        = (1 << 1),
  kLECodedPHY                           = (1 << 2),
  kLEExtendedAdvertising                = (1 << 3),
  kLEPeriodicAdvertising                = (1 << 4),
  kChannelSelectionAlgorithm2           = (1 << 5),
  kLEPowerClass1                        = (1 << 6),
  kMinimumNumberOfUsedChannelsProcedure = (1 << 7),

  // The rest is reserved for future use.
};

// Binary values that can be generically passed to HCI commands that expect a
// 1-octet boolean "enable"/"disable" parameter.
enum class GenericEnableParam : uint8_t {
  kDisable = 0x00,
  kEnable = 0x01,
};

// The minimum and maximum range values for the LE advertising interval
// parameters.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.5)
constexpr uint16_t kLEAdvertisingIntervalMin = 0x0020;
constexpr uint16_t kLEAdvertisingIntervalMax = 0x4000;

// The default LE advertising interval parameter value, corresponding to 1.28
// seconds (see Core Spec v5.0, Vol 2, Part E, Section 7.8.5).
constexpr uint16_t kLEAdvertisingIntervalDefault = 0x0800;

// The minimum and maximum range values for the LE scan interval parameters.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.10)
constexpr uint16_t kLEScanIntervalMin = 0x0004;
constexpr uint16_t kLEScanIntervalMax = 0x4000;

// The default LE scan interval parameter value, corresponding to 10
// milliseconds (see Core Spec v5.0, Vol 2, Part E, Section 7.8.10).
constexpr uint16_t kLEScanIntervalDefault = 0x0010;

// LE advertising types (see Core Spec v5.0, Vol 2, Part E, Section 7.8.5).
enum class LEAdvertisingType : uint8_t {
  // ADV_IND: Connectable and scannable undirected advertising (the default
  // value used by the controller).
  kAdvInd = 0x00,

  // ADV_DIRECT_IND (high duty cycle): Connectable high duty cycle directed
  // advertising.
  kAdvDirectIndHighDutyCycle = 0x01,

  // ADV_SCAN_IND: Scannable undirected advertising.
  kAdvScanInd = 0x02,

  // ADV_NONCONN_IND: Non-connectable undirected advertising.
  kAdvNonConnInd = 0x03,

  // ADV_DIRECT_IND (low duty cycle): Connectable low duty cycle advertising.
  kAdvDirectIndLowDutyCycle = 0x04,

  // The rest is reserved for future use
};

// Possible values that can be used for the |own_address_type| parameter in a
// HCI_LE_Set_Advertising_Parameters or a HCI_LE_Set_Scan_Parameters command.
// (see Core Spec v5.0, Vol 2, Part E, Sections 7.8.5 and 7.8.10)
enum class LEOwnAddressType : uint8_t {
  // Public device address (default)
  kPublic = 0x00,

  // Random device address
  kRandom = 0x01,

  // Controller generates Resolvable Private Address based on the local IRK from
  // the resolving list. If the resolving list contains no matching entry, use
  // the public address.
  kPrivateDefaultToPublic = 0x02,

  // Controller generates Resolvable Private Address based on the local IRK from
  // the resolving list. If the resolving list contains no matching entry, use
  // the random address from LE_Set_Random_Address.
  kPrivateDefaultToRandom = 0x03,

  // The rest is reserved for future use
};

// Possible values that can be used for the |peer_address_type| parameter in a
// HCI_LE_Set_Advertising_Parameters command.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.5)
enum class LEPeerAddressType : uint8_t {
  // Public Device Address (default) or Public Identity Address
  kPublic = 0x00,

  // Random Device Address or Random (static) Identity Address
  kRandom = 0x01,

  // The rest is reserved for future use
};

// Possible values that can be used for the |adv_channel_map| bitfield in a
// HCI_LE_Set_Advertising_Parameters command.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.5)
constexpr uint8_t kLEAdvertisingChannel37 = 0x01;
constexpr uint8_t kLEAdvertisingChannel38 = 0x02;
constexpr uint8_t kLEAdvertisingChannel39 = 0x04;
constexpr uint8_t kLEAdvertisingChannelAll = 0x07;

// Possible values that can be used for the |adv_filter_policy| parameter in a
// HCI_LE_Set_Advertising_Parameters command.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.5)
enum class LEAdvFilterPolicy : uint8_t {
  // Process scan and connection requests from all devices (i.e., the White List
  // is not in use) (default).
  kAllowAll = 0x00,

  // Process connection requests from all devices and only scan requests from
  // devices that are in the White List.
  kConnAllScanWhiteList = 0x01,

  // Process scan requests from all devices and only connection requests from
  // devices that are in the White List.
  kScanAllConnWhiteList = 0x02,

  // Process scan and connection requests only from devices in the White List.
  kWhiteListOnly = 0x03,

  // The rest is reserved for future use.
};

// Possible values that can be used for the |scan_type| parameter in a
// LE_Set_Scan_Parameters command.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.10)
enum class LEScanType : uint8_t {
  // Passive Scanning. No scanning PDUs shall be sent (default)
  kPassive = 0x00,

  // Active scanning. Scanning PDUs may be sent.
  kActive = 0x01,

  // The rest is reserved for future use.
};

// Possible values that can be used for the |filter_policy| parameter
// in a HCI_LE_Set_Scan_Parameters command.
// (see Core Spec v5.0, Vol 2, Part E, Section 7.8.10)
enum class LEScanFilterPolicy : uint8_t {
  // Accept all advertising packets except directed advertising packets not
  // addressed to this device (default).
  kNoWhiteList = 0x00,

  // Accept only advertising packets from devices where the advertiser’s address
  // is in the White List. Directed advertising packets which are not addressed
  // to this device shall be ignored.
  kUseWhiteList = 0x01,

  // Accept all advertising packets except directed advertising packets where
  // the initiator's identity address does not address this device.
  // Note: Directed advertising packets where the initiator's address is a
  // resolvable private address that cannot be resolved are also accepted.
  kNoWhiteListWithPrivacy = 0x02,

  // Accept all advertising packets except:
  //
  //   - advertising packets where the advertiser's identity address is not in
  //     the White List; and
  //
  //   - directed advertising packets where the initiator's identity address
  //     does not address this device
  //
  // Note: Directed advertising packets where the initiator's address is a
  // resolvable private address that cannot be resolved are also accepted.
  kUseWhiteListWithPrivacy = 0x03,
};

// The maximum length of advertising data that can get passed to the
// HCI_LE_Set_Advertising_Data command.
constexpr size_t kMaxLEAdvertisingDataLength = 0x1F;  // (31)

// The maximum length of Local Name that can be assigned to a BR/EDR controller,
// in octets.
constexpr size_t kMaxLocalNameLength = 248;

// The maximum number of bytes in a HCI Command Packet payload, excluding the
// header. See Core Spec v5.0 Vol 2, Part E, 5.4.1, paragraph 2.
constexpr size_t kMaxCommandPacketPayloadSize = 255;

// The maximum number of bytes in a HCI event Packet payload, excluding the
// header. See Core Spec v5.0 Vol 2, Part E, 5.4.4, paragraph 1.
constexpr size_t kMaxEventPacketPayloadSize = 255;

// Values that can be used in HCI Read|WriteFlowControlMode commands.
enum class FlowControlMode : uint8_t {
  // Packet based data flow control mode (default for a Primary Controller)
  kPacketBased = 0x00,

  // Data block based flow control mode (default for an AMP Controller)
  kDataBlockBased = 0x01
};

}  // namespace hci
}  // namespace bluetooth
