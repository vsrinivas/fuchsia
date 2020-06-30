// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {std::convert::TryFrom, thiserror::Error};

/// Decoding error type
#[derive(Error, Debug, PartialEq)]
pub enum DecodingError {
    /// The value that was sent on the wire was out of range.
    #[error("Value was out of range")]
    OutOfRange,
}

/// Generates an enum value where each variant can be converted into a constant in the given
/// raw_type.  For example:
/// pub_decodable_enum! {
///     Color<u8, Error> {
///        Red => 1,
///        Blue => 2,
///        Green => 3,
///     }
/// }
/// Then Color::try_from(2) returns Color::Red, and u8::from(Color::Red) returns 1.
#[macro_export]
macro_rules! pub_decodable_enum {
    ($(#[$meta:meta])* $name:ident<$raw_type:ty,$error_type:ident,$error_path:ident> {
        $($(#[$variant_meta:meta])* $variant:ident => $val:expr),*,
    }) => {
        $(#[$meta])*
        #[derive(Debug, Eq, Hash, PartialEq, Copy, Clone)]
        pub enum $name {
            $($(#[$variant_meta])* $variant = $val),*
        }

        $crate::tofrom_decodable_enum! {
            $name<$raw_type, $error_type, $error_path> {
                $($variant => $val),*,
            }
        }

        impl $name {
            pub fn name(&self) -> &'static str {
                match self {
                    $($name::$variant => stringify!($variant)),*
                }
            }
        }
    }
}

/// A From<&$name> for $raw_type implementation and
/// TryFrom<$raw_type> for $name implementation, used by (pub_)decodable_enum
#[macro_export]
macro_rules! tofrom_decodable_enum {
    ($name:ident<$raw_type:ty, $error_type:ident, $error_path:ident> {
        $($variant:ident => $val:expr),*,
    }) => {
        impl From<&$name> for $raw_type {
            fn from(v: &$name) -> $raw_type {
                match v {
                    $($name::$variant => $val),*,
                }
            }
        }

        impl TryFrom<$raw_type> for $name {
            type Error = $error_type;
            fn try_from(value: $raw_type) -> std::result::Result<Self, $error_type> {
                match value {
                    $($val => Ok($name::$variant)),*,
                    _ => Err($error_type::$error_path),
                }
            }
        }
    }
}

pub_decodable_enum! {
    /// An enum of status error codes we can receive from the hardware.
    /// BLUETOOTH CORE SPECIFICATION Version 5.2 | Vol 1, Part F
    #[allow(dead_code)]
    StatusCode <u8, DecodingError, OutOfRange> {
        Success                                      => 0x00,
        UnknownCommand                               => 0x01,
        UnknownConnectionId                          => 0x02,
        HardwareFailure                              => 0x03,
        PageTimeout                                  => 0x04,
        AuthenticationFailure                        => 0x05,
        PinOrKeyMissing                              => 0x06,
        MemoryCapacityExceeded                       => 0x07,
        ConnectionTimeout                            => 0x08,
        ConnectionLimitExceeded                      => 0x09,
        SynchronousConnectionLimitExceeded           => 0x0A,
        ConnectionAlreadyExists                      => 0x0B,
        CommandDisallowed                            => 0x0C,
        ConnectionRejectedLimitedResources           => 0x0D,
        ConnectionRejectedSecurity                   => 0x0E,
        ConnectionRejectedBadBdAddr                  => 0x0F,
        ConnectionAcceptTimeoutExceeded              => 0x10,
        UnsupportedFeatureOrParameter                => 0x11,
        InvalidHCICommandParameters                  => 0x12,
        RemoteUserTerminatedConnection               => 0x13,
        RemoteDeviceTerminatedConnectionLowResources => 0x14,
        RemoteDeviceTerminatedConnectionPowerOff     => 0x15,
        ConnectionTerminatedByLocalHost              => 0x16,
        RepeatedAttempts                             => 0x17,
        PairingNotAllowed                            => 0x18,
        UnknownLMPPDU                                => 0x19,
        UnsupportedRemoteFeature                     => 0x1A,
        SCOOffsetRejected                            => 0x1B,
        SCOIntervalRejected                          => 0x1C,
        SCOAirModeRejected                           => 0x1D,
        InvalidLMPOrLLParameters                     => 0x1E,
        UnspecifiedError                             => 0x1F,
        UnsupportedLMPOrLLParameterValue             => 0x20,
        RoleChangeNotAllowed                         => 0x21,
        LMPOrLLResponseTimeout                       => 0x22,
        LMPErrorTransactionCollision                 => 0x23,
        LMPPDUNotAllowed                             => 0x24,
        EncryptionModeNotAcceptable                  => 0x25,
        LinkKeyCannotBeChanged                       => 0x26,
        RequestedQoSNotSupported                     => 0x27,
        InstantPassed                                => 0x28,
        PairingWithUnitKeyNotSupported               => 0x29,
        DifferentTransactionCollision                => 0x2A,
        Reserved0                                    => 0x2B,
        QoSUnacceptableParameter                     => 0x2C,
        QoSRejected                                  => 0x2D,
        ChannelClassificationNotSupported            => 0x2E,
        InsufficientSecurity                         => 0x2F,
        ParameterOutOfMandatoryRange                 => 0x30,
        Reserved1                                    => 0x31,
        RoleSwitchPending                            => 0x32,
        Reserved2                                    => 0x33,
        ReservedSlotViolation                        => 0x34,
        RoleSwitchFailed                             => 0x35,
        ExtendedInquiryResponseTooLarge              => 0x36,
        SecureSimplePairingNotSupportedByHost        => 0x37,
        HostBusyPairing                              => 0x38,
        ConnectionRejectedNoSuitableChannelFound     => 0x39,
        ControllerBusy                               => 0x3A,
        UnacceptableConnectionParameters             => 0x3B,
        DirectedAdvertisingTimeout                   => 0x3C,
        ConnectionTerminatedMICFailure               => 0x3D,
        ConnectionFailedToBeEstablished              => 0x3E,
        MACConnectionFailed                          => 0x3F,
        CoarseClockAdjustmentRejected                => 0x40,
        Type0SubmapNotDefined                        => 0x41,
        UnknownAdvertisingIdentifier                 => 0x42,
        LimitReached                                 => 0x43,
        OperationCancelledByHost                     => 0x44,
    }
}
