// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::u8;

use crate::packets::player_application_settings::PlayerApplicationSettingAttributeId;
use crate::packets::*;

#[derive(Debug)]
/// AVRCP 1.6.1 section 6.5.5 GetPlayerApplicationSettingAttributeText
pub struct GetPlayerApplicationSettingAttributeTextCommand {
    num_attributes: u8,
    attribute_ids: Vec<PlayerApplicationSettingAttributeId>,
}

impl GetPlayerApplicationSettingAttributeTextCommand {
    pub fn new(
        attribute_ids: Vec<PlayerApplicationSettingAttributeId>,
    ) -> GetPlayerApplicationSettingAttributeTextCommand {
        Self { num_attributes: attribute_ids.len() as u8, attribute_ids }
    }
}

impl VendorDependent for GetPlayerApplicationSettingAttributeTextCommand {
    fn pdu_id(&self) -> PduId {
        PduId::GetPlayerApplicationSettingAttributeText
    }
}

impl VendorCommand for GetPlayerApplicationSettingAttributeTextCommand {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Status
    }
}

impl Decodable for GetPlayerApplicationSettingAttributeTextCommand {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessage);
        }

        let num_attributes = buf[0];
        // There must be at least 1 attribute ID provided.
        // See AVRCP Sec 6.5.3
        if num_attributes < 1 {
            return Err(Error::InvalidMessage);
        }

        let mut attribute_ids = vec![];
        let mut chunks = buf[1..].chunks(1);
        while let Some(chunk) = chunks.next() {
            attribute_ids.push(PlayerApplicationSettingAttributeId::try_from(chunk[0])?);
        }

        if attribute_ids.len() != num_attributes as usize {
            return Err(Error::InvalidMessage);
        }
        Ok(Self { num_attributes, attribute_ids })
    }
}

impl Encodable for GetPlayerApplicationSettingAttributeTextCommand {
    fn encoded_len(&self) -> usize {
        1 + self.num_attributes as usize
    }
    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::OutOfRange);
        }
        buf[0] = u8::from(self.num_attributes);
        if self.num_attributes as usize != self.attribute_ids.len() {
            return Err(Error::Encoding);
        }
        for (i, id) in self.attribute_ids.iter().enumerate() {
            buf[i + 1] = u8::from(id);
        }
        Ok(())
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct AttributeInfo {
    attribute_id: PlayerApplicationSettingAttributeId,
    character_set_id: CharsetId,
    attribute_string_length: u8,
    attribute_string: Vec<u8>,
}

impl AttributeInfo {
    pub fn new(
        attribute_id: PlayerApplicationSettingAttributeId,
        character_set_id: CharsetId,
        attribute_string_length: u8,
        attribute_string: Vec<u8>,
    ) -> Self {
        Self { attribute_id, character_set_id, attribute_string_length, attribute_string }
    }

    // The size of `AttributeInfo` in bytes.
    // 1 byte for `attribute_id`, 2 bytes for `character_set_id`, 1 byte for
    // `attribute_string_length`, `attribute_string_length` bytes for `attribute_string`.
    pub fn num_bytes(&self) -> usize {
        4 + self.attribute_string_length as usize
    }
}

#[derive(Debug)]
/// AVRCP 1.6.1 section 6.5.5 GetPlayerApplicationSettingAttributeText
pub struct GetPlayerApplicationSettingAttributeTextResponse {
    num_attributes: u8,
    attribute_infos: Vec<AttributeInfo>,
}

impl GetPlayerApplicationSettingAttributeTextResponse {
    #[allow(dead_code)]
    pub fn new(
        attribute_infos: Vec<AttributeInfo>,
    ) -> GetPlayerApplicationSettingAttributeTextResponse {
        Self { num_attributes: attribute_infos.len() as u8, attribute_infos }
    }
}

impl VendorDependent for GetPlayerApplicationSettingAttributeTextResponse {
    fn pdu_id(&self) -> PduId {
        PduId::GetPlayerApplicationSettingAttributeText
    }
}

impl VendorCommand for GetPlayerApplicationSettingAttributeTextResponse {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Status
    }
}

impl Decodable for GetPlayerApplicationSettingAttributeTextResponse {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessage);
        }

        let num_attributes = buf[0];
        // There must be at least 1 attribute ID provided.
        // See AVRCP Sec 6.5.5
        if num_attributes < 1 {
            return Err(Error::InvalidMessage);
        }
        let mut attribute_infos: Vec<AttributeInfo> = Vec::new();

        let mut buf_idx: usize = 1;

        while buf_idx + 4 < buf.len() {
            let attribute_id: PlayerApplicationSettingAttributeId =
                PlayerApplicationSettingAttributeId::try_from(buf[buf_idx])?;
            let charset_id: u16 = ((buf[buf_idx + 1] as u16) << 8) | (buf[buf_idx + 2] as u16);
            let charset_id: CharsetId = CharsetId::try_from(charset_id)?;

            let str_length: usize = buf[buf_idx + 3] as usize;
            if (buf_idx + 4 + str_length) > buf.len() {
                return Err(Error::OutOfRange);
            }
            let mut attribute_string = vec![0; str_length];
            attribute_string.copy_from_slice(&buf[buf_idx + 4..buf_idx + 4 + str_length]);

            let attribute_info =
                AttributeInfo::new(attribute_id, charset_id, str_length as u8, attribute_string);

            attribute_infos.push(attribute_info);

            buf_idx += 4 + str_length;
        }

        if attribute_infos.len() != num_attributes as usize {
            return Err(Error::InvalidMessage);
        }

        Ok(Self { num_attributes, attribute_infos })
    }
}

impl Encodable for GetPlayerApplicationSettingAttributeTextResponse {
    fn encoded_len(&self) -> usize {
        let mut len: usize = 1;
        for attr_info in &self.attribute_infos {
            len += attr_info.num_bytes();
        }
        len
    }
    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::OutOfRange);
        }

        buf[0] = u8::from(self.num_attributes);

        // There must be at least 1 attribute ID provided.
        // See AVRCP Sec 6.5.5
        if self.num_attributes < 1 {
            return Err(Error::Encoding);
        }

        if self.num_attributes as usize != self.attribute_infos.len() {
            return Err(Error::Encoding);
        }
        let mut idx = 1;
        for info in self.attribute_infos.iter() {
            buf[idx] = u8::from(&info.attribute_id);
            let charset_id = u16::from(&info.character_set_id);
            buf[idx + 1] = (charset_id >> 8) as u8;
            buf[idx + 2] = charset_id as u8;
            buf[idx + 3] = info.attribute_string_length;
            for (i, str_byte) in info.attribute_string.iter().enumerate() {
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
    // Test GetPlayerApplicationSettingAttributeTextCommand encoding success.
    fn test_get_player_application_setting_attribute_text_command_encode() {
        let command = GetPlayerApplicationSettingAttributeTextCommand::new(vec![
            PlayerApplicationSettingAttributeId::Equalizer,
        ]);
        assert_eq!(command.pdu_id(), PduId::GetPlayerApplicationSettingAttributeText);
        assert_eq!(command.encoded_len(), 2); // 1 + 1 for the 1 id.
        let mut buf = vec![0; command.encoded_len()];
        assert!(command.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, vec![0x01, 0x01]);
    }

    #[test]
    // Test GetPlayerApplicationSettingAttributeTextCommand encoding failure due
    // to too small buffer.
    fn test_get_player_application_setting_attribute_text_command_encode_error() {
        let command = GetPlayerApplicationSettingAttributeTextCommand::new(vec![
            PlayerApplicationSettingAttributeId::Equalizer,
        ]);
        assert_eq!(command.pdu_id(), PduId::GetPlayerApplicationSettingAttributeText);
        assert_eq!(command.encoded_len(), 2);
        // Smaller buffer size.
        let mut buf = vec![0; command.encoded_len() - 1];
        assert!(command.encode(&mut buf[..]).is_err());
    }

    #[test]
    // Test GetPlayerApplicationSettingAttributeTextCommand decoding success.
    fn test_get_player_application_setting_attribute_text_command_decode() {
        let command = [0x01, 0x01];
        let result = GetPlayerApplicationSettingAttributeTextCommand::decode(&command);
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(result.num_attributes, 1);
        assert_eq!(result.attribute_ids, vec![PlayerApplicationSettingAttributeId::Equalizer]);
    }

    #[test]
    // Test GetPlayerApplicationSettingAttributeTextCommand decoding failure due
    // to invalid `num_attributes` in buffer.
    fn test_get_player_application_setting_attribute_text_command_decode_error() {
        let command = [0x02, 0x01];
        let result = GetPlayerApplicationSettingAttributeTextCommand::decode(&command);
        assert!(result.is_err());
    }

    #[test]
    // Test GetPlayerApplicationSettingAttributeTextResponse encoding success.
    fn test_get_player_application_setting_attribute_text_response_encode() {
        let attr_infos = vec![
            // 10 bytes.
            AttributeInfo::new(
                PlayerApplicationSettingAttributeId::Equalizer,
                CharsetId::Ascii,
                6,
                "Foobar".as_bytes().to_vec(),
            ),
            // 8 bytes.
            AttributeInfo::new(
                PlayerApplicationSettingAttributeId::ScanMode,
                CharsetId::Utf8,
                4,
                "test".as_bytes().to_vec(),
            ),
        ];
        let response = GetPlayerApplicationSettingAttributeTextResponse::new(attr_infos);
        assert_eq!(response.pdu_id(), PduId::GetPlayerApplicationSettingAttributeText);
        assert_eq!(response.encoded_len(), 19);
        let mut buf = vec![0; response.encoded_len()];
        assert!(response.encode(&mut buf[..]).is_ok());
        assert_eq!(
            buf,
            vec![
                0x02, 0x01, 0x00, 0x03, 0x06, 0x46, 0x6F, 0x6F, 0x62, 0x61, 0x72, 0x04, 0x00, 0x6A,
                0x04, 0x74, 0x65, 0x73, 0x74,
            ]
        );
    }

    #[test]
    // Test GetPlayerApplicationSettingAttributeTextResponse decoding success.
    fn test_get_player_application_setting_attribute_text_response_decode() {
        let command = [0x01, 0x02, 0x00, 0x04, 0x02, 0x62, 0x78];
        let result = GetPlayerApplicationSettingAttributeTextResponse::decode(&command);
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(result.num_attributes, 1);
        let expected_info = AttributeInfo::new(
            PlayerApplicationSettingAttributeId::RepeatStatusMode,
            CharsetId::Iso8859_1,
            2,
            "bx".as_bytes().to_vec(),
        );
        assert_eq!(result.attribute_infos, vec![expected_info]);
    }

    #[test]
    // Test GetPlayerApplicationSettingAttributeTextResponse decoding failure due
    // to invalid CharsetId.
    fn test_get_player_application_setting_attribute_text_response_decode_error() {
        let command = [0x01, 0x02, 0xFF, 0xFF, 0x02, 0x62, 0x78];
        let result = GetPlayerApplicationSettingAttributeTextResponse::decode(&command);
        assert!(result.is_err());
    }

    #[test]
    // Test GetPlayerApplicationSettingAttributeTextResponse decoding failure due
    // to invalid `num_attributes`.
    fn test_get_player_application_setting_attribute_text_response_decode_attrs_error() {
        let command = [0x02, 0x02, 0x00, 0x04, 0x02, 0x62, 0x78];
        let result = GetPlayerApplicationSettingAttributeTextResponse::decode(&command);
        assert!(result.is_err());
    }

    #[test]
    // Test GetPlayerApplicationSettingAttributeTextResponse decoding failure due
    // to invalid `attribute_id`.
    fn test_get_player_application_setting_attribute_text_response_decode_attr_id_error() {
        let command = [0x01, 0xFF, 0x00, 0x04, 0x02, 0x62, 0x78];
        let result = GetPlayerApplicationSettingAttributeTextResponse::decode(&command);
        assert!(result.is_err());
    }

    #[test]
    // Test GetPlayerApplicationSettingAttributeTextResponse decoding failure due
    // to buffer that is too small.
    fn test_get_player_application_setting_attribute_text_response_decode_buf_error() {
        let command = [0x01, 0x01, 0x00, 0x04, 0x05, 0x02, 0x04];
        let result = GetPlayerApplicationSettingAttributeTextResponse::decode(&command);
        assert!(result.is_err());
    }
}
