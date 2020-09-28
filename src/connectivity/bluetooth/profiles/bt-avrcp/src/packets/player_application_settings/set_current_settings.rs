// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::u8;

use super::*;
use crate::packets;
use crate::packets::player_application_settings::PlayerApplicationSettingAttributeId;
use crate::packets::*;

/// Packet format for a SetPlayerApplicationSettingValue command.
/// See AVRCP Sec 6.5.4
#[derive(Debug)]
pub struct SetPlayerApplicationSettingValueCommand {
    num_player_application_setting_attributes: u8,
    attribute_id_values: Vec<(PlayerApplicationSettingAttributeId, u8)>,
}

impl SetPlayerApplicationSettingValueCommand {
    #[allow(dead_code)]
    pub fn new(
        settings: Vec<(PlayerApplicationSettingAttributeId, u8)>,
    ) -> SetPlayerApplicationSettingValueCommand {
        let num_player_application_setting_attributes = settings.len() as u8;
        Self { num_player_application_setting_attributes, attribute_id_values: settings }
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for SetPlayerApplicationSettingValueCommand {
    fn pdu_id(&self) -> PduId {
        PduId::SetPlayerApplicationSettingValue
    }
}

/// Specifies the AVC command type for this AVC command packet
impl VendorCommand for SetPlayerApplicationSettingValueCommand {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Control
    }
}

impl Decodable for SetPlayerApplicationSettingValueCommand {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessage);
        }
        let num_player_application_setting_attributes = buf[0];
        // There must be at least 1 attribute (ID,Value) pair provided.
        // See AVRCP Sec 6.5.4
        if num_player_application_setting_attributes < 1 {
            return Err(Error::InvalidMessage);
        }
        let mut idx = 1;
        let mut attribute_id_values = vec![];
        while idx + 1 < buf.len() {
            let attribute_id: u8 = buf[idx];
            let value: u8 = buf[idx + 1];
            attribute_id_values
                .push((PlayerApplicationSettingAttributeId::try_from(attribute_id)?, value));
            idx += 2;
        }
        if attribute_id_values.len() != num_player_application_setting_attributes as usize {
            return Err(Error::InvalidMessage);
        }
        Ok(Self { num_player_application_setting_attributes, attribute_id_values })
    }
}

impl Encodable for SetPlayerApplicationSettingValueCommand {
    fn encoded_len(&self) -> usize {
        1 + 2 * self.num_player_application_setting_attributes as usize
    }
    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::InvalidMessageLength);
        }
        if self.num_player_application_setting_attributes as usize != self.attribute_id_values.len()
        {
            return Err(Error::ParameterEncodingError);
        }
        buf[0] = u8::from(self.num_player_application_setting_attributes);
        let mut idx: usize = 0;
        let mut buf_idx: usize = 1;
        while idx < self.num_player_application_setting_attributes as usize {
            let (id, val) = self.attribute_id_values[idx];
            buf[buf_idx] = u8::from(&id);
            buf[buf_idx + 1] = val;
            idx += 1;
            buf_idx += 2;
        }
        Ok(())
    }
}

impl TryFrom<SetPlayerApplicationSettingValueCommand> for PlayerApplicationSettings {
    type Error = packets::Error;
    fn try_from(
        src: SetPlayerApplicationSettingValueCommand,
    ) -> Result<PlayerApplicationSettings, Error> {
        let mut setting = PlayerApplicationSettings::new(None, None, None, None);
        if src.num_player_application_setting_attributes as usize != src.attribute_id_values.len() {
            return Err(Error::InvalidMessage);
        }
        for (attribute_id, value) in src.attribute_id_values {
            // TODO(fxbug.dev/41253): If fetching the attribute_id fails, check to see if it's
            // a valid custom attribute. Handle accordingly.
            let attribute_id = PlayerApplicationSettingAttributeId::try_from(attribute_id)
                .map_err(|_| Error::InvalidMessage)?;
            match attribute_id {
                PlayerApplicationSettingAttributeId::Equalizer => {
                    let pas_value: Equalizer =
                        Equalizer::try_from(value).map_err(|_| Error::InvalidMessage)?;
                    setting.equalizer = Some(pas_value);
                }
                PlayerApplicationSettingAttributeId::RepeatStatusMode => {
                    let pas_value: RepeatStatusMode =
                        RepeatStatusMode::try_from(value).map_err(|_| Error::InvalidMessage)?;
                    setting.repeat_status_mode = Some(pas_value);
                }
                PlayerApplicationSettingAttributeId::ShuffleMode => {
                    let pas_value: ShuffleMode =
                        ShuffleMode::try_from(value).map_err(|_| Error::InvalidMessage)?;
                    setting.shuffle_mode = Some(pas_value);
                }
                PlayerApplicationSettingAttributeId::ScanMode => {
                    let pas_value: ScanMode =
                        ScanMode::try_from(value).map_err(|_| Error::InvalidMessage)?;
                    setting.scan_mode = Some(pas_value);
                }
            }
        }
        Ok(setting)
    }
}

/// Packet format for a SetPlayerApplicationSettingValue response.
/// The response is simply an acknowledgement, so it's empty.
/// See AVRCP Sec 6.5.4
#[derive(Debug)]
pub struct SetPlayerApplicationSettingValueResponse {}

impl SetPlayerApplicationSettingValueResponse {
    pub fn new() -> SetPlayerApplicationSettingValueResponse {
        Self {}
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for SetPlayerApplicationSettingValueResponse {
    fn pdu_id(&self) -> PduId {
        PduId::SetPlayerApplicationSettingValue
    }
}

impl Decodable for SetPlayerApplicationSettingValueResponse {
    fn decode(_buf: &[u8]) -> PacketResult<Self> {
        Ok(Self {})
    }
}

impl Encodable for SetPlayerApplicationSettingValueResponse {
    fn encoded_len(&self) -> usize {
        0
    }
    fn encode(&self, _buf: &mut [u8]) -> PacketResult<()> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    // Test SetPlayerApplicationSettingValue command encoding success.
    fn test_set_player_application_setting_value_command_encode() {
        let settings = PlayerApplicationSettings::new(
            None,
            None,
            Some(ShuffleMode::GroupShuffle),
            Some(ScanMode::Off),
        );
        let settings_vec = settings_to_vec(&settings);
        let response = SetPlayerApplicationSettingValueCommand::new(settings_vec);
        assert_eq!(response.raw_pdu_id(), u8::from(&PduId::SetPlayerApplicationSettingValue));
        assert_eq!(response.command_type(), AvcCommandType::Control);
        assert_eq!(response.encoded_len(), 5);
        let mut buf = vec![0; response.encoded_len()];
        assert!(response.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, vec![0x02, 0x03, 0x03, 0x04, 0x01]);
    }

    #[test]
    // Test SetPlayerApplicationSettingValue command decoding success.
    fn test_set_player_application_setting_value_command_decode() {
        let command = [0x03, 0x01, 0x03, 0x02, 0x01, 0x03, 0x01];
        let result = SetPlayerApplicationSettingValueCommand::decode(&command);
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(result.num_player_application_setting_attributes, 3);
        assert_eq!(
            result.attribute_id_values,
            vec![
                (PlayerApplicationSettingAttributeId::Equalizer, 0x03),
                (PlayerApplicationSettingAttributeId::RepeatStatusMode, 0x01),
                (PlayerApplicationSettingAttributeId::ShuffleMode, 0x01)
            ]
        );
    }

    #[test]
    // Test SetPlayerApplicationSettingValue command decoding error.
    fn test_set_player_application_setting_value_command_decode_error() {
        // Missing final PAS value associated with attribute.
        let command = [0x03, 0x01, 0x03, 0x02, 0x01, 0x03];
        let result = SetPlayerApplicationSettingValueCommand::decode(&command);
        assert!(result.is_err());
    }

    #[test]
    // Test SetPlayerApplicationSettingValue response encoding success.
    fn test_set_player_application_setting_value_response_encode() {
        let response = SetPlayerApplicationSettingValueResponse::new();
        assert_eq!(response.raw_pdu_id(), u8::from(&PduId::SetPlayerApplicationSettingValue));
        assert_eq!(response.encoded_len(), 0); // empty payload for command
        let mut buf = vec![0; response.encoded_len()];
        assert!(response.encode(&mut buf[..]).is_ok());
        assert_eq!(buf.len(), 0);
    }

    #[test]
    // Test SetPlayerApplicationSettingValue response decoding success.
    fn test_set_player_application_setting_value_response_decode() {
        let res = SetPlayerApplicationSettingValueResponse::decode(&[]);
        assert!(res.is_ok());
    }
}
