// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use super::*;

use thiserror::Error;

/// The error types during decode
#[derive(Error, Debug)]
pub enum DecodeError {
    #[error("Invalid panel key value")]
    PassthroughInvalidPanelKey,

    #[error("Vendor Specific Command with invalid header")]
    VendorInvalidPreamble(u8, PacketError),

    #[error("Vendor Specific Command with an unsupported PDU")]
    VendorPduNotImplemented(u8),

    #[error("Vendor Specific Command that is not a notify, status, or control packet")]
    VendorPacketTypeNotImplemented(AvcPacketType),

    #[error("Vendor Specific Command ")]
    VendorPacketDecodeError(AvcCommandType, PduId, PacketError),
}

macro_rules! decoder_enum {
    ($decode_command_type:ident<$cmd_type:ident> { $($command:ident => $decoder:ident, )+ }) => {
        #[derive(Debug)]
        pub enum $decode_command_type {
            $(
                $command($decoder),
            )+
        }

        impl $decode_command_type {
            pub fn pdu_id(&self) -> PduId {
                match self {
                    $(
                        $decode_command_type::$command(_) => PduId::$command,
                    )+
                }
            }

            fn decode_command(
                pdu_id: PduId,
                body: &[u8],
            ) -> Result<$decode_command_type, DecodeError> {

                let match_pdu = || -> Result<Option<$decode_command_type>, PacketError> {
                    Ok(
                        match pdu_id {
                            $(
                                PduId::$command => Some($decode_command_type::$command(
                                    $decoder::decode(body)?
                                )),
                            )+
                            _=> None
                        }
                    )
                };

                match match_pdu() {
                    Ok(Some(command)) => Ok(command),
                    Ok(None) => {
                        fx_vlog!(tag: "avrcp", 2, "Received known but unhandled vendor command {:?}", pdu_id);
                        Err(DecodeError::VendorPduNotImplemented(u8::from(&pdu_id)))
                    }
                    Err(e) => {
                        fx_vlog!(tag: "avrcp", 2, "Unable to decode vendor packet {:?}", pdu_id);
                        Err(DecodeError::VendorPacketDecodeError(AvcCommandType::$cmd_type, pdu_id, e))
                    }
                }

            }
        }
    };
}

decoder_enum!(
    StatusCommand<Status> {
        GetCapabilities => GetCapabilitiesCommand,
        ListPlayerApplicationSettingAttributes => ListPlayerApplicationSettingAttributesCommand,
        ListPlayerApplicationSettingValues => ListPlayerApplicationSettingValuesCommand,
        GetCurrentPlayerApplicationSettingValue => GetCurrentPlayerApplicationSettingValueCommand,
        GetPlayerApplicationSettingAttributeText => GetPlayerApplicationSettingAttributeTextCommand,
        GetPlayerApplicationSettingValueText => GetPlayerApplicationSettingValueTextCommand,
        GetElementAttributes => GetElementAttributesCommand,
        GetPlayStatus => GetPlayStatusCommand,
    }
);

decoder_enum!(
    ControlCommand<Control> {
        SetPlayerApplicationSettingValue => SetPlayerApplicationSettingValueCommand,
        RequestContinuingResponse => RequestContinuingResponseCommand,
        AbortContinuingResponse => AbortContinuingResponseCommand,
        SetAbsoluteVolume => SetAbsoluteVolumeCommand,
    }
);

#[derive(Debug)]
pub enum VendorSpecificCommand {
    // The only valid Notify command type is a `RegisterNotificationCommand`
    Notify(RegisterNotificationCommand),
    Status(StatusCommand),
    Control(ControlCommand),
}

impl VendorSpecificCommand {
    fn decode_command(
        packet_body: &[u8],
        packet_type: AvcPacketType,
    ) -> Result<VendorSpecificCommand, DecodeError> {
        // decode the packet header
        let preamble = match VendorDependentPreamble::decode(packet_body) {
            Err(e) => {
                let mut pdu_id = 0;
                if packet_body.len() > 0 {
                    // if we have partial preamble, see if we can at least parse the first pdu for
                    // the reject response
                    pdu_id = packet_body[0];
                }
                return Err(DecodeError::VendorInvalidPreamble(pdu_id, e));
            }
            Ok(x) => x,
        };

        let offset = preamble.encoded_len();

        let end = offset + preamble.parameter_length as usize;
        if packet_body.len() < end {
            fx_log_err!(
                "Received command is truncated. expected: {}, Received: {}",
                preamble.parameter_length,
                end - packet_body.len()
            );
        }

        let body = &packet_body[preamble.encoded_len()..];

        // decode fields from the header.
        // See if we know this PDU ID. If not we will respond with a not implemented error.
        let pdu_id = match PduId::try_from(preamble.pdu_id) {
            Err(_) => {
                return Err(DecodeError::VendorPduNotImplemented(preamble.pdu_id));
            }
            Ok(x) => x,
        };

        // The packet should be either a CONTROL, STATUS, or NOTIFY command otherwise return an
        // error.
        match packet_type {
            AvcPacketType::Command(AvcCommandType::Notify) => {
                fx_vlog!(tag: "avrcp", 2, "Received ctype=notify command {:?}", pdu_id);

                // The only PDU that you can send a Notify on is RegisterNotification.
                if pdu_id != PduId::RegisterNotification {
                    return Err(DecodeError::VendorPduNotImplemented(u8::from(&pdu_id)));
                }

                match RegisterNotificationCommand::decode(&body[..]) {
                    Ok(notify_command) => Ok(VendorSpecificCommand::Notify(notify_command)),
                    Err(e) => {
                        Err(DecodeError::VendorPacketDecodeError(AvcCommandType::Notify, pdu_id, e))
                    }
                }
            }
            AvcPacketType::Command(AvcCommandType::Status) => {
                fx_vlog!(tag: "avrcp", 2, "Received ctype=status command {:?}", pdu_id);
                Ok(VendorSpecificCommand::Status(StatusCommand::decode_command(pdu_id, body)?))
            }
            AvcPacketType::Command(AvcCommandType::Control) => {
                fx_vlog!(tag: "avrcp", 2, "Received ctype=command command {:?}", pdu_id);
                Ok(VendorSpecificCommand::Control(ControlCommand::decode_command(pdu_id, body)?))
            }
            _ => {
                fx_vlog!(tag: "avrcp", 2, "Received unhandled packet type");
                Err(DecodeError::VendorPacketTypeNotImplemented(packet_type))
            }
        }
    }
}

#[derive(Debug)]
pub enum Command {
    Passthrough { command: AvcPanelCommand, pressed: bool },
    VendorSpecific(VendorSpecificCommand),
}

impl Command {
    /// Decodes the panel command and it's pressed value. Returns an error if the key is not recognized.
    fn decode_passthrough_command(body: &[u8]) -> Result<Command, DecodeError> {
        if body.len() < 1 {
            return Err(DecodeError::PassthroughInvalidPanelKey);
        }

        let key = body[0] & 0x7f;
        let pressed = body[0] & 0x80 == 0;

        if let Some(command) = AvcPanelCommand::from_primitive(key) {
            Ok(Command::Passthrough { command, pressed })
        } else {
            Err(DecodeError::PassthroughInvalidPanelKey)
        }
    }

    pub fn decode_command(
        body: &[u8],
        packet_type: AvcPacketType,
        op_code: &AvcOpCode,
    ) -> Result<Command, DecodeError> {
        Ok(match op_code {
            &AvcOpCode::VendorDependent => {
                Command::VendorSpecific(VendorSpecificCommand::decode_command(body, packet_type)?)
            }
            &AvcOpCode::Passthrough => Command::decode_passthrough_command(body)?,
            _ => panic!("other states handled at AVCTP level"),
        })
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use matches::assert_matches;

    // AvcCommand is hard to mock
    #[test]
    fn test_passthrough_decoder_key_pressed() {
        let body: &[u8] = &[AvcPanelCommand::AppsMenu.into_primitive(), 0x00];
        let result = Command::decode_passthrough_command(body);
        assert_matches!(
            result,
            Ok(Command::Passthrough { command: AvcPanelCommand::AppsMenu, pressed: true })
        )
    }

    #[test]
    fn test_passthrough_decoder_key_released() {
        // set high bit to signal key release
        let body: &[u8] = &[AvcPanelCommand::Help.into_primitive() | 0x80, 0x00];
        let result = Command::decode_passthrough_command(body);
        assert_matches!(
            result,
            Ok(Command::Passthrough { command: AvcPanelCommand::Help, pressed: false })
        )
    }

    #[test]
    fn test_passthrough_decoder_invalid_key() {
        let body: &[u8] = &[16u8, 0x00]; // 16 is not a valid key command
        let result = Command::decode_passthrough_command(body);
        assert_matches!(result, Err(DecodeError::PassthroughInvalidPanelKey))
    }

    // test we are filtering vendor status commands properly and calling the decode method properly
    #[test]
    fn test_vendor_status_command() {
        // generic vendor status command
        let packet = &[
            0x20, // GetElementAttributes pdu id
            0x00, // single packet
            0x00, 0x11, // param len, 17 bytes
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // NOW_PLAYING identifier
            0x02, // 2 attributes
            0x00, 0x00, 0x00, 0x01, // Title
            0x00, 0x00, 0x00, 0x02, // ArtistName
        ];

        if let Ok(VendorSpecificCommand::Status(StatusCommand::GetElementAttributes(
            get_element_attributes,
        ))) = VendorSpecificCommand::decode_command(
            packet,
            AvcPacketType::Command(AvcCommandType::Status),
        ) {
            assert_eq!(
                get_element_attributes.attributes(),
                &[MediaAttributeId::Title, MediaAttributeId::ArtistName]
            );
        } else {
            panic!("not a get element attribute command");
        }
    }

    // test with a bad status command we get an error we expect
    #[test]
    fn test_vendor_status_command_decode_fail() {
        // generic vendor status command
        let packet = &[
            0x20, // GetElementAttributes pdu id
            0x00, // single packet
            0x00, 0x05, // param len, INVALID 5 bytes
            0x00, 0x00, 0x00, 0x00, 0x00, // partial identifier
        ];

        let result = VendorSpecificCommand::decode_command(
            packet,
            AvcPacketType::Command(AvcCommandType::Status),
        );

        assert_matches!(
            result,
            Err(DecodeError::VendorPacketDecodeError(
                AvcCommandType::Status,
                PduId::GetElementAttributes,
                PacketError::InvalidMessageLength
            ))
        );
    }

    // test with an unknown PDU
    #[test]
    fn test_vendor_invalid_pdu() {
        // generic vendor status command
        let packet = &[
            0x66, // Invalid PDU ID
            0x00, // single packet
            0x00, 0x05, // param len
            0x00, 0x00, 0x00, 0x00, 0x00, // junk for test
        ];

        let result = VendorSpecificCommand::decode_command(
            packet,
            AvcPacketType::Command(AvcCommandType::Status),
        );

        assert_matches!(result, Err(DecodeError::VendorPduNotImplemented(_)));
    }

    // test a valid packet, but on the wrong command type
    #[test]
    fn test_vendor_invalid_command_type() {
        // generic vendor status command
        let packet = &[
            0x20, // GetElementAttributes pdu id
            0x00, // single packet
            0x00, 0x11, // param len, 17 bytes
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // NOW_PLAYING identifier
            0x02, // 2 attributes
            0x00, 0x00, 0x00, 0x01, // Title
            0x00, 0x00, 0x00, 0x02, // ArtistName
        ];

        let result = VendorSpecificCommand::decode_command(
            packet,
            AvcPacketType::Command(AvcCommandType::Notify),
        );

        assert_matches!(result, Err(DecodeError::VendorPduNotImplemented(_)));
    }

    // test a valid packet, but on the wrong command type (specific_inquiry which is not used in BT)
    #[test]
    fn test_vendor_invalid_packet_type() {
        // generic vendor status command
        let packet = &[
            0x20, // GetElementAttributes pdu id
            0x00, // single packet
            0x00, 0x11, // param len, 17 bytes
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // NOW_PLAYING identifier
            0x02, // 2 attributes
            0x00, 0x00, 0x00, 0x01, // Title
            0x00, 0x00, 0x00, 0x02, // ArtistName
        ];

        let result = VendorSpecificCommand::decode_command(
            packet,
            AvcPacketType::Command(AvcCommandType::SpecificInquiry),
        );

        assert_matches!(result, Err(DecodeError::VendorPacketTypeNotImplemented(_)));
    }
}
