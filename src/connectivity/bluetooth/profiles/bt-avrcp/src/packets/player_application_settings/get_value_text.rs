// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::u8;

use crate::packets::player_application_settings::PlayerApplicationSettingAttributeId;
use crate::packets::*;

#[derive(Debug)]
/// AVRCP 1.6.1 section 6.5.6 GetPlayerApplicationSettingValueText command.
pub struct GetPlayerApplicationSettingValueTextCommand {
    attribute_id: PlayerApplicationSettingAttributeId,
    num_values: u8,
    value_ids: Vec<u8>,
}

impl GetPlayerApplicationSettingValueTextCommand {
    pub fn new(
        attribute_id: PlayerApplicationSettingAttributeId,
        value_ids: Vec<u8>,
    ) -> GetPlayerApplicationSettingValueTextCommand {
        Self { attribute_id, num_values: value_ids.len() as u8, value_ids }
    }
}

impl VendorDependent for GetPlayerApplicationSettingValueTextCommand {
    fn pdu_id(&self) -> PduId {
        PduId::GetPlayerApplicationSettingValueText
    }
}

impl VendorCommand for GetPlayerApplicationSettingValueTextCommand {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Status
    }
}

impl Decodable for GetPlayerApplicationSettingValueTextCommand {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 2 {
            return Err(Error::InvalidMessage);
        }

        let attribute_id = PlayerApplicationSettingAttributeId::try_from(buf[0])?;
        let num_values = buf[1];

        // There must be at least 1 attribute value id provided.
        // See AVRCP Sec 6.5.6
        if num_values < 1 {
            return Err(Error::InvalidMessage);
        }

        let mut value_ids = vec![];
        let mut idx = 2;
        while idx < buf.len() {
            value_ids.push(buf[idx]);
            idx += 1;
        }

        if num_values as usize != value_ids.len() {
            return Err(Error::InvalidMessage);
        }
        Ok(Self { attribute_id, num_values, value_ids })
    }
}

impl Encodable for GetPlayerApplicationSettingValueTextCommand {
    fn encoded_len(&self) -> usize {
        2 + self.num_values as usize
    }
    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::OutOfRange);
        }

        buf[0] = u8::from(&self.attribute_id);
        buf[1] = self.num_values;

        if self.num_values != self.value_ids.len() as u8 {
            return Err(Error::Encoding);
        }

        for (i, v_id) in self.value_ids.iter().enumerate() {
            buf[i + 2] = u8::from(v_id.clone());
        }

        Ok(())
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct ValueInfo {
    value_id: u8,
    character_set_id: CharsetId,
    value_string_length: u8,
    value_string: Vec<u8>,
}

impl ValueInfo {
    pub fn new(
        value_id: u8,
        character_set_id: CharsetId,
        value_string_length: u8,
        value_string: Vec<u8>,
    ) -> Self {
        Self { value_id, character_set_id, value_string_length, value_string }
    }

    // The size of `ValueInfo` in bytes.
    // 1 byte for `value_id`, 2 bytes for `character_set_id`, 1 byte for
    // `value_string_length`, `value_string_length` bytes for `value_string`.
    pub fn num_bytes(&self) -> usize {
        4 + self.value_string_length as usize
    }
}

#[derive(Debug)]
/// AVRCP 1.6.1 section 6.5.6 GetPlayerApplicationSettingValueText response.
pub struct GetPlayerApplicationSettingValueTextResponse {
    num_values: u8,
    value_infos: Vec<ValueInfo>,
}

impl GetPlayerApplicationSettingValueTextResponse {
    #[allow(dead_code)]
    pub fn new(value_infos: Vec<ValueInfo>) -> GetPlayerApplicationSettingValueTextResponse {
        Self { num_values: value_infos.len() as u8, value_infos }
    }
}

impl VendorDependent for GetPlayerApplicationSettingValueTextResponse {
    fn pdu_id(&self) -> PduId {
        PduId::GetPlayerApplicationSettingValueText
    }
}

impl VendorCommand for GetPlayerApplicationSettingValueTextResponse {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Status
    }
}

impl Decodable for GetPlayerApplicationSettingValueTextResponse {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessage);
        }

        let num_values = buf[0];
        // There must be at least 1 value ID provided.
        // See AVRCP Sec 6.5.6
        if num_values < 1 {
            return Err(Error::InvalidMessage);
        }
        let mut value_infos: Vec<ValueInfo> = Vec::new();

        let mut buf_idx: usize = 1;

        while buf_idx + 4 < buf.len() {
            let value_id = buf[buf_idx];
            let charset_id: u16 = ((buf[buf_idx + 1] as u16) << 8) | (buf[buf_idx + 2] as u16);
            let charset_id: CharsetId = CharsetId::try_from(charset_id)?;

            let str_length: usize = buf[buf_idx + 3] as usize;
            if (buf_idx + 4 + str_length) > buf.len() {
                return Err(Error::OutOfRange);
            }
            let mut value_string = vec![0; str_length];
            value_string.copy_from_slice(&buf[buf_idx + 4..buf_idx + 4 + str_length]);
            let value_info = ValueInfo::new(value_id, charset_id, str_length as u8, value_string);

            value_infos.push(value_info);

            buf_idx += 4 + str_length;
        }

        if value_infos.len() != num_values as usize {
            return Err(Error::InvalidMessage);
        }

        Ok(Self { num_values, value_infos })
    }
}

impl Encodable for GetPlayerApplicationSettingValueTextResponse {
    fn encoded_len(&self) -> usize {
        let mut len: usize = 1;
        for value_info in &self.value_infos {
            len += value_info.num_bytes();
        }
        len
    }
    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::OutOfRange);
        }

        buf[0] = u8::from(self.num_values);

        // There must be at least 1 value ID provided.
        // See AVRCP Sec 6.5.6
        if self.num_values < 1 {
            return Err(Error::Encoding);
        }

        if self.num_values as usize != self.value_infos.len() {
            return Err(Error::Encoding);
        }
        let mut idx = 1;
        for info in self.value_infos.iter() {
            buf[idx] = u8::from(info.value_id);
            let charset_id = u16::from(&info.character_set_id);
            buf[idx + 1] = (charset_id >> 8) as u8;
            buf[idx + 2] = charset_id as u8;
            buf[idx + 3] = info.value_string_length;
            for (i, str_byte) in info.value_string.iter().enumerate() {
                buf[idx + 4 + i] = str_byte.clone();
            }
            idx += info.num_bytes();
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    // Test GetPlayerApplicationSettingValueTextCommand encoding success.
    fn test_get_player_application_setting_value_text_command_encode() {
        let command = GetPlayerApplicationSettingValueTextCommand::new(
            PlayerApplicationSettingAttributeId::Equalizer,
            vec![0x01, 0x02],
        );
        assert_eq!(command.pdu_id(), PduId::GetPlayerApplicationSettingValueText);
        assert_eq!(command.encoded_len(), 4);
        let mut buf = vec![0; command.encoded_len()];
        assert!(command.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, vec![0x01, 0x02, 0x01, 0x02]);
    }

    #[test]
    // Test GetPlayerApplicationSettingValueTextCommand encoding failure due
    // to too small buffer.
    fn test_get_player_application_setting_value_text_command_encode_error() {
        let command = GetPlayerApplicationSettingValueTextCommand::new(
            PlayerApplicationSettingAttributeId::Equalizer,
            vec![0x01, 0x02],
        );
        assert_eq!(command.pdu_id(), PduId::GetPlayerApplicationSettingValueText);
        assert_eq!(command.encoded_len(), 4);
        // Smaller buffer size.
        let mut buf = vec![0; command.encoded_len() - 1];
        assert!(command.encode(&mut buf[..]).is_err());
    }

    #[test]
    // Test GetPlayerApplicationSettingValueTextCommand decoding success.
    fn test_get_player_application_setting_value_text_command_decode() {
        let command = [0x02, 0x03, 0x01, 0x02, 0x03];
        let result = GetPlayerApplicationSettingValueTextCommand::decode(&command);
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(result.attribute_id, PlayerApplicationSettingAttributeId::RepeatStatusMode);
        assert_eq!(result.num_values, 3);
        assert_eq!(result.value_ids, vec![0x01, 0x02, 0x03]);
    }

    #[test]
    // Test GetPlayerApplicationSettingValueTextCommand decoding failure due
    // to invalid `attribute_id` in buffer.
    fn test_get_player_application_setting_value_text_command_decode_error() {
        let command = [0x09, 0x03, 0x01, 0x02, 0x03];
        let result = GetPlayerApplicationSettingValueTextCommand::decode(&command);
        assert!(result.is_err());
    }

    #[test]
    // Test GetPlayerApplicationSettingValueTextCommand decoding failure due
    // to invalid buffer.
    fn test_get_player_application_setting_value_text_command_decode_buf_error() {
        let command = [0x03, 0x04, 0x01, 0x02, 0x03];
        let result = GetPlayerApplicationSettingValueTextCommand::decode(&command);
        assert!(result.is_err());
    }

    #[test]
    // Test GetPlayerApplicationSettingValueTextResponse encoding success.
    fn test_get_player_application_setting_value_text_response_encode() {
        let value_infos = vec![
            // 10 bytes.
            ValueInfo::new(0x01, CharsetId::Utf16, 6, "abcdef".as_bytes().to_vec()),
            // 8 bytes.
            ValueInfo::new(0x04, CharsetId::Utf8, 4, "plop".as_bytes().to_vec()),
            // 5 bytes.
            ValueInfo::new(0x05, CharsetId::Ucs2, 1, "t".as_bytes().to_vec()),
        ];
        let response = GetPlayerApplicationSettingValueTextResponse::new(value_infos);
        assert_eq!(response.pdu_id(), PduId::GetPlayerApplicationSettingValueText);
        assert_eq!(response.encoded_len(), 24);
        let mut buf = vec![0; response.encoded_len()];
        assert!(response.encode(&mut buf[..]).is_ok());
        assert_eq!(
            buf,
            vec![
                0x03, 0x01, 0x03, 0xF7, 0x06, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x04, 0x00, 0x6A,
                0x04, 0x70, 0x6C, 0x6F, 0x70, 0x05, 0x03, 0xE8, 0x01, 0x74,
            ]
        );
    }

    #[test]
    // Test GetPlayerApplicationSettingValueTextResponse decoding success.
    fn test_get_player_application_setting_value_text_response_decode() {
        let command = [0x01, 0x10, 0x03, 0xF7, 0x05, 0x61, 0x62, 0x63, 0x64, 0x65];
        let result = GetPlayerApplicationSettingValueTextResponse::decode(&command);
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(result.num_values, 1);
        let expected_info = ValueInfo::new(0x10, CharsetId::Utf16, 5, "abcde".as_bytes().to_vec());
        assert_eq!(result.value_infos, vec![expected_info]);
    }

    #[test]
    // Test GetPlayerApplicationSettingValueTextResponse decoding failure due
    // to invalid CharsetId.
    fn test_get_player_application_setting_value_text_response_decode_error() {
        let command = [0x01, 0x02, 0xFF, 0xFF, 0x02, 0x62, 0x78];
        let result = GetPlayerApplicationSettingValueTextResponse::decode(&command);
        assert!(result.is_err());
    }

    #[test]
    // Test GetPlayerApplicationSettingValueTextResponse decoding failure due
    // to invalid `num_attributes`.
    fn test_get_player_application_setting_value_text_response_decode_attrs_error() {
        let command = [0x02, 0x02, 0x00, 0x04, 0x02, 0x62, 0x78];
        let result = GetPlayerApplicationSettingValueTextResponse::decode(&command);
        assert!(result.is_err());
    }

    #[test]
    // Test GetPlayerApplicationSettingValueTextResponse decoding failure due
    // to buffer that is too small.
    fn test_get_player_application_setting_value_text_response_decode_buf_error() {
        let command = [0x01, 0x01, 0x00, 0x04, 0x05, 0x01];
        let result = GetPlayerApplicationSettingValueTextResponse::decode(&command);
        assert!(result.is_err());
    }
}
