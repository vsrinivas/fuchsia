// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    std::convert::{TryFrom, TryInto},
    std::ops::Range,
    std::u16,
    thiserror::Error,
};

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
    /// HCI Event Codes
    EventPacketType <u8, DecodingError, OutOfRange> {
        InquiryComplete => 0x01,
        InquiryResult => 0x02,
        CommandComplete => 0x0E,
        CommandStatus => 0x0F,
    }
}

// Command groups
pub const LINK_CONTROL_COMMAND: u8 = 0x01;
pub const CONTROLLER_AND_BASEBAND_COMMAND: u8 = 0x03;
pub const LE_CONTROLLER_COMMAND: u8 = 0x08;

/// Represents an Opcode
/// OGF Range (6 bits): 0x00 to 0x3F (0x3F reserved for vendor-specific debug commands)
/// OCF Range (10 bits): 0x0000 to 0x03FF
#[derive(Debug, PartialEq)]
pub struct Opcode(u8, u16);

impl TryFrom<&[u8]> for Opcode {
    type Error = DecodingError;

    fn try_from(buf: &[u8]) -> Result<Self, Self::Error> {
        if buf.len() < 2 {
            return Err(DecodingError::OutOfRange);
        }
        let opcode = decode_opcode(buf);
        let ogf = (opcode >> 10) as u8;
        let ocf = opcode & 0x3FF;
        Ok(Opcode(ogf, ocf))
    }
}

impl From<Opcode> for u16 {
    fn from(opcode: Opcode) -> Self {
        ((opcode.0 as u16 & 0x3F) << 10) | (opcode.1 & 0x03FF)
    }
}

/// Command opcodes (not comprehensive)
pub enum CommandOpcode {
    /// Inquiry Command (v1.1) (BR/EDR)
    /// Core Spec v5.0, Vol 2, Part E, Section 7.1
    Inquiry,
    /// Inquiry Cancel Command (v1.1) (BR/EDR)
    /// Core Spec v5.0, Vol 2, Part E, Section 7.1
    InquiryCancel,
    /// Set Event Mask Command (v1.1)
    /// Core Spec v5.0 Vol 2, Part E, Section 7.3
    SetEventMask,
    /// Reset Command (v1.1)
    /// Core Spec v5.0 Vol 2, Part E, Section 7.3
    Reset,
    /// LE Set Scan Parameters Command (v4.0) (LE)
    /// Core Spec v5.0 Vol 2, Part E, Section 7.8
    LESetScanParameters,
    /// LE Set Scan Enable Command (v4.0) (LE)
    /// Core Spec v5.0 Vol 2, Part E, Section 7.8
    LESetScanEnable,
}

impl From<CommandOpcode> for Opcode {
    fn from(command_opcode: CommandOpcode) -> Self {
        match command_opcode {
            CommandOpcode::Inquiry => Opcode(LINK_CONTROL_COMMAND, 0x0001),
            CommandOpcode::InquiryCancel => Opcode(LINK_CONTROL_COMMAND, 0x0002),
            CommandOpcode::SetEventMask => Opcode(CONTROLLER_AND_BASEBAND_COMMAND, 0x0001),
            CommandOpcode::Reset => Opcode(CONTROLLER_AND_BASEBAND_COMMAND, 0x0003),
            CommandOpcode::LESetScanParameters => Opcode(LE_CONTROLLER_COMMAND, 0x000B),
            CommandOpcode::LESetScanEnable => Opcode(LE_CONTROLLER_COMMAND, 0x000C),
        }
    }
}

pub trait EncodableCommand {
    fn encode(&self) -> Vec<u8>;
}

#[derive(Debug)]
pub struct ResetCommand {}

impl ResetCommand {
    pub fn new() -> Self {
        Self {}
    }
}

impl EncodableCommand for ResetCommand {
    fn encode(&self) -> Vec<u8> {
        encode_command(CommandOpcode::Reset.into(), &[]).expect("unable to encode command")
    }
}

#[derive(Debug)]
pub struct InquiryCommand {
    pub max_results: u8,
    pub max_interval: u8,
}

// Per Core Spec v5.0, Vol 2, Part E, Section 7.1,
// interval is N * 1.28 seconds. Interval max is 30.
const INQUIRY_SECONDS_PER_INTERVAL: f32 = 1.28;
const INQUIRY_INTERVAL_MAX: u8 = 30;

impl InquiryCommand {
    pub fn new(timeout: u8, max_results: u8) -> Self {
        let max_interval = std::cmp::min(
            std::cmp::max((timeout as f32 / INQUIRY_SECONDS_PER_INTERVAL).floor() as u8, 1),
            INQUIRY_INTERVAL_MAX,
        );
        Self { max_results, max_interval }
    }
}

impl EncodableCommand for InquiryCommand {
    fn encode(&self) -> Vec<u8> {
        // Iquiry params
        let payload = [
            0x33,
            0x8B,
            0x9E,              // LAP: General/Unlimited Inquiry Access Code
            self.max_interval, // Inquiry_Length N * 1.28 seconds
            self.max_results,  // Num_Responses
        ];

        encode_command(CommandOpcode::Inquiry.into(), &payload).expect("unable to encode command")
    }
}

fn encode_command(opcode: Opcode, payload: &[u8]) -> Result<Vec<u8>, Error> {
    if payload.len() > 255 {
        return Err(format_err!("payload too large to encode"));
    }

    let opcode_val: u16 = opcode.into();
    let opcode_bytes = opcode_val.to_le_bytes();
    let mut packet = vec![];
    packet.extend_from_slice(&opcode_bytes);
    packet.push(payload.len() as u8);
    packet.extend_from_slice(&payload);
    Ok(packet)
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

/// Decode opcode from slice (2 bytes as u16 LE)
pub fn decode_opcode(bytes: &[u8]) -> u16 {
    assert!(bytes.len() >= 2);
    return ((bytes[1] as u16) << 8) + bytes[0] as u16;
}

/// An raw opaque command. Used relative to an external buffer.
#[derive(Debug)]
pub struct RawCommand {
    /// HCI opcode
    pub opcode: u16,

    /// Payload length
    pub paramater_total_length: u8,

    /// Range of the entire HCI command, relative to the original buffer
    pub range: Range<usize>,
}

/// Decode commands from a buffer. Validates the commands are intact and valid.
pub fn split_commands(bytes: &[u8]) -> Result<Vec<RawCommand>, Error> {
    let mut offset = 0;
    let mut commands = vec![];

    loop {
        if bytes.len() < offset + 3 {
            return Err(Error::msg("Invalid packet length"));
        }

        let opcode = decode_opcode(&bytes[offset..]);
        let paramater_total_length = bytes[offset + 2];
        let end = offset + 3 + paramater_total_length as usize;

        if bytes.len() < end {
            return Err(Error::msg(format!(
                "Packet too short: {} {}",
                paramater_total_length,
                end - offset
            )));
        }

        commands.push(RawCommand { opcode, paramater_total_length, range: (offset..end) });

        offset = end;
        if bytes.len() == end {
            break;
        }
    }

    Ok(commands)
}

/// Inquiry Result
#[derive(Debug)]
pub struct InquiryResult {
    pub br_addr: [u8; 6],
    pub page_scan_repetition_mode: u8,
    pub class_of_device: [u8; 3],
    pub clockoffset: u16,
}

// br_addr(6) + page_scan_repetition_mode(1) + reserved(2) + class_of_device(3) + clockoffset(2)
const INQUIRY_RESULT_LENGTH: usize = 6 + 1 + 2 + 3 + 2;

pub fn parse_inquiry_result(payload: &[u8]) -> Vec<InquiryResult> {
    let mut results = vec![];

    for result in payload[3..].chunks_exact(INQUIRY_RESULT_LENGTH) {
        results.push(InquiryResult {
            br_addr: result[0..6].try_into().expect("invalid length"),
            page_scan_repetition_mode: result[6],
            class_of_device: result[9..12].try_into().expect("invalid length"),
            clockoffset: u16::from_le_bytes(result[12..14].try_into().expect("invalid length")),
        });
    }

    results
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_split_commands_noargs() {
        let _ = split_commands(&[]).expect_err("Split error");
    }

    #[test]
    fn test_invalid_arg_too_short() {
        let _ = split_commands(&[0x01, 0x02]).expect_err("Split error");
    }

    #[test]
    fn test_invalid_arg_invalid_second_packet() {
        // first cmd with no params. second is truncated.
        let _ = split_commands(&[0x01, 0x02, 0x00, 0x01]).expect_err("Split error");
    }

    #[test]
    fn test_split_commands_one_arg() {
        let mut cmds = split_commands(&[0x03, 0x0c, 0x00]).expect("Split error");
        // single command, no payload
        assert_eq!(cmds.len(), 1);
        let cmd = cmds.pop().expect("Expected command");
        assert_eq!(cmd.opcode, 0x0c03);
        assert_eq!(cmd.range, (0..3));
        assert_eq!(cmd.paramater_total_length, 0);
    }

    #[test]
    fn test_split_commands_multiple_arg() {
        let mut cmds = split_commands(&[0x03, 0x0c, 0x00, 0xff, 0x22, 0x03, 0x0a, 0x0b, 0xc])
            .expect("Split error");

        // 2 commands. first command zero payload. second command 3 param payload.

        assert_eq!(cmds.len(), 2);

        let cmd = cmds.pop().expect("Expected command");
        assert_eq!(cmd.opcode, 0x22ff);
        assert_eq!(cmd.range, (3..9));
        assert_eq!(cmd.paramater_total_length, 3);

        let cmd = cmds.pop().expect("Expected command");
        assert_eq!(cmd.opcode, 0xc03);
        assert_eq!(cmd.range, (0..3));
        assert_eq!(cmd.paramater_total_length, 0);
    }

    #[test]
    fn test_decode_opcode() {
        assert_eq!(decode_opcode(&[0x02, 0x01]), 0x0102);
        assert_eq!(decode_opcode(&[0xff, 0x00]), 0x00ff);
    }

    #[test]
    fn test_reset_command() {
        let reset_cmd = ResetCommand::new().encode();
        assert_eq!(reset_cmd, vec![0x03, 0x0C, 0x00]);
    }

    #[test]
    fn test_inquiry_command() {
        let inquiry_cmd = InquiryCommand::new(20, 30).encode();
        assert_eq!(
            inquiry_cmd,
            vec![
                0x01, 0x04, // opcode
                0x05, // param len 5
                0x33, 0x8B, 0x9E, // LAP
                0x0F, // timeout (floor(20/1.28))
                0x1E, // max results
            ]
        );
    }
}
