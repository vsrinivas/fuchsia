// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::u8;

use crate::packets::player_application_settings::PlayerApplicationSettingAttributeId;
use crate::packets::*;

/// Packet format for a ListPlayerApplicationSettingAttributes command.
/// See AVRCP Sec 6.5.1
#[derive(Debug)]
pub struct ListPlayerApplicationSettingAttributesCommand {}

impl ListPlayerApplicationSettingAttributesCommand {
    #[allow(dead_code)]
    pub fn new() -> ListPlayerApplicationSettingAttributesCommand {
        Self {}
    }
}

impl VendorDependent for ListPlayerApplicationSettingAttributesCommand {
    fn pdu_id(&self) -> PduId {
        PduId::ListPlayerApplicationSettingAttributes
    }
}

impl VendorCommand for ListPlayerApplicationSettingAttributesCommand {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Status
    }
}

impl Decodable for ListPlayerApplicationSettingAttributesCommand {
    fn decode(_buf: &[u8]) -> PacketResult<Self> {
        Ok(Self {})
    }
}

impl Encodable for ListPlayerApplicationSettingAttributesCommand {
    fn encoded_len(&self) -> usize {
        0
    }
    fn encode(&self, _buf: &mut [u8]) -> PacketResult<()> {
        Ok(())
    }
}

/// Packet format for a ListPlayerApplicationSettingAttributes response.
/// See AVRCP Sec 6.5.1
#[derive(Debug)]
pub struct ListPlayerApplicationSettingAttributesResponse {
    num_player_application_setting_attributes: u8,
    player_application_setting_attribute_ids: Vec<PlayerApplicationSettingAttributeId>,
}

impl ListPlayerApplicationSettingAttributesResponse {
    #[allow(dead_code)]
    pub fn new(
        num_player_application_setting_attributes: u8,
        player_application_setting_attribute_ids: Vec<PlayerApplicationSettingAttributeId>,
    ) -> ListPlayerApplicationSettingAttributesResponse {
        Self { num_player_application_setting_attributes, player_application_setting_attribute_ids }
    }
    #[allow(dead_code)]
    pub fn num_player_application_setting_attributes(&self) -> u8 {
        self.num_player_application_setting_attributes
    }
    #[allow(dead_code)]
    pub fn player_application_setting_attribute_ids(
        &self,
    ) -> Vec<PlayerApplicationSettingAttributeId> {
        self.player_application_setting_attribute_ids.clone()
    }
}

impl VendorDependent for ListPlayerApplicationSettingAttributesResponse {
    fn pdu_id(&self) -> PduId {
        PduId::ListPlayerApplicationSettingAttributes
    }
}

impl Decodable for ListPlayerApplicationSettingAttributesResponse {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessage);
        }
        let num_player_application_setting_attributes = buf[0];
        let mut player_application_setting_attribute_ids = vec![];
        if num_player_application_setting_attributes > 0 {
            let mut chunks = buf[1..].chunks(1);
            while let Some(chunk) = chunks.next() {
                player_application_setting_attribute_ids
                    .push(PlayerApplicationSettingAttributeId::try_from(chunk[0])?);
            }
        }
        if player_application_setting_attribute_ids.len()
            != num_player_application_setting_attributes as usize
        {
            return Err(Error::InvalidMessage);
        }
        Ok(Self {
            num_player_application_setting_attributes,
            player_application_setting_attribute_ids,
        })
    }
}

impl Encodable for ListPlayerApplicationSettingAttributesResponse {
    fn encoded_len(&self) -> usize {
        1 + self.num_player_application_setting_attributes as usize
    }
    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::OutOfRange);
        }
        buf[0] = self.num_player_application_setting_attributes;
        for (i, id) in self.player_application_setting_attribute_ids.iter().enumerate() {
            buf[i + 1] = u8::from(id);
        }
        Ok(())
    }
}

/// Packet format for a ListPlayerApplicationSettingValues command.
/// See AVRCP Sec 6.5.2
#[derive(Debug)]
pub struct ListPlayerApplicationSettingValuesCommand {
    player_application_setting_attribute_id: PlayerApplicationSettingAttributeId,
}

impl ListPlayerApplicationSettingValuesCommand {
    #[allow(dead_code)]
    pub fn new(
        player_application_setting_attribute_id: PlayerApplicationSettingAttributeId,
    ) -> ListPlayerApplicationSettingValuesCommand {
        Self { player_application_setting_attribute_id }
    }
}

impl VendorDependent for ListPlayerApplicationSettingValuesCommand {
    fn pdu_id(&self) -> PduId {
        PduId::ListPlayerApplicationSettingValues
    }
}

impl VendorCommand for ListPlayerApplicationSettingValuesCommand {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Status
    }
}

impl Decodable for ListPlayerApplicationSettingValuesCommand {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessage);
        }
        let player_application_setting_attribute_id =
            PlayerApplicationSettingAttributeId::try_from(buf[0])?;
        return Ok(Self { player_application_setting_attribute_id });
    }
}

impl Encodable for ListPlayerApplicationSettingValuesCommand {
    fn encoded_len(&self) -> usize {
        1
    }
    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::OutOfRange);
        }
        buf[0] = u8::from(&self.player_application_setting_attribute_id);
        Ok(())
    }
}

/// Packet format for a ListPlayerApplicationSettingValues response.
/// See AVRCP Sec 6.5.2
#[derive(Debug)]
pub struct ListPlayerApplicationSettingValuesResponse {
    #[allow(dead_code)]
    num_player_application_setting_values: u8,
    player_application_setting_value_ids: Vec<u8>,
}

impl ListPlayerApplicationSettingValuesResponse {
    #[allow(dead_code)]
    pub fn new(
        num_player_application_setting_values: u8,
        player_application_setting_value_ids: Vec<u8>,
    ) -> ListPlayerApplicationSettingValuesResponse {
        Self { num_player_application_setting_values, player_application_setting_value_ids }
    }
    #[allow(dead_code)]
    pub fn player_application_setting_value_ids(&self) -> Vec<u8> {
        self.player_application_setting_value_ids.clone()
    }
}

impl VendorDependent for ListPlayerApplicationSettingValuesResponse {
    fn pdu_id(&self) -> PduId {
        PduId::ListPlayerApplicationSettingValues
    }
}

impl Decodable for ListPlayerApplicationSettingValuesResponse {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessage);
        }
        let num_player_application_setting_values = buf[0];
        // Per AVRCP Sec 6.5.2, there must be at least one setting_value returned.
        if num_player_application_setting_values < 1 {
            return Err(Error::InvalidMessage);
        }
        let mut player_application_setting_value_ids = vec![];
        let mut chunks = buf[1..].chunks(1);
        while let Some(chunk) = chunks.next() {
            player_application_setting_value_ids.push(chunk[0]);
        }
        if player_application_setting_value_ids.len()
            != num_player_application_setting_values as usize
        {
            return Err(Error::InvalidMessage);
        }
        Ok(Self { num_player_application_setting_values, player_application_setting_value_ids })
    }
}

impl Encodable for ListPlayerApplicationSettingValuesResponse {
    fn encoded_len(&self) -> usize {
        1 + self.num_player_application_setting_values as usize
    }
    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::OutOfRange);
        }
        buf[0] = self.num_player_application_setting_values;
        for (i, id) in self.player_application_setting_value_ids.iter().enumerate() {
            buf[i + 1] = id.clone();
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    // Test ListPlayerApplicationSettingAttributes command encoding success.
    fn test_list_player_application_setting_attributes_command_encode() {
        let attributes = ListPlayerApplicationSettingAttributesCommand::new();
        assert_eq!(attributes.pdu_id(), PduId::ListPlayerApplicationSettingAttributes);
        assert_eq!(attributes.command_type(), AvcCommandType::Status);
        assert_eq!(attributes.encoded_len(), 0); // empty payload for command
        let mut buf = vec![0; attributes.encoded_len()];
        assert!(attributes.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, vec![]);
    }

    #[test]
    // Test ListPlayerApplicationSettingAttributes command decoding success.
    fn test_list_player_application_setting_attributes_command_decode() {
        let res = ListPlayerApplicationSettingAttributesCommand::decode(&[]);
        assert!(res.is_ok());
    }

    #[test]
    // Test ListPlayerApplicationSettingAttributes response encoding success.
    fn test_list_player_application_setting_attributes_response_encode() {
        let response = ListPlayerApplicationSettingAttributesResponse::new(
            2,
            vec![
                PlayerApplicationSettingAttributeId::Equalizer,
                PlayerApplicationSettingAttributeId::RepeatStatusMode,
            ],
        );
        assert_eq!(response.pdu_id(), PduId::ListPlayerApplicationSettingAttributes);
        // 1 + 2 for the 2 ids.
        assert_eq!(response.encoded_len(), 3);
        let mut buf = vec![0; response.encoded_len()];
        assert!(response.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, vec![0x02, 0x01, 0x02]);
    }

    #[test]
    // Test ListPlayerApplicationSettingAttributes response decoding success.
    fn test_list_player_application_setting_attributes_response_decode() {
        let response = [0x02, 0x01, 0x02];
        let result = ListPlayerApplicationSettingAttributesResponse::decode(&response);
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(result.num_player_application_setting_attributes(), 2);
        assert_eq!(
            result.player_application_setting_attribute_ids(),
            vec![
                PlayerApplicationSettingAttributeId::Equalizer,
                PlayerApplicationSettingAttributeId::RepeatStatusMode,
            ]
        );
    }

    #[test]
    // Test ListPlayerApplicationSettingValues command encoding success.
    fn test_list_player_application_setting_values_command_encode() {
        let attribute_id = ListPlayerApplicationSettingValuesCommand::new(
            PlayerApplicationSettingAttributeId::ScanMode,
        );
        assert_eq!(attribute_id.pdu_id(), PduId::ListPlayerApplicationSettingValues);
        assert_eq!(attribute_id.command_type(), AvcCommandType::Status);
        // Empty payload for command
        assert_eq!(attribute_id.encoded_len(), 1);
        let mut buf = vec![0; attribute_id.encoded_len()];
        assert!(attribute_id.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, vec![0x04]);
    }

    #[test]
    // Test ListPlayerApplicationSettingValues command decoding success.
    fn test_list_player_application_setting_values_command_decode() {
        let response = [0x02];
        let result = ListPlayerApplicationSettingValuesCommand::decode(&response);
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(
            result.player_application_setting_attribute_id,
            PlayerApplicationSettingAttributeId::RepeatStatusMode
        );
    }

    #[test]
    // Test ListPlayerApplicationSettingValues response encoding success.
    fn test_list_player_application_setting_values_response_encode() {
        let response = ListPlayerApplicationSettingValuesResponse::new(2, vec![0x01, 0x03]);
        assert_eq!(response.pdu_id(), PduId::ListPlayerApplicationSettingValues);
        // 1 + 2 for the 2 id's.
        assert_eq!(response.encoded_len(), 3);
        let mut buf = vec![0; response.encoded_len()];
        assert!(response.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, vec![0x02, 0x01, 0x03]);
    }

    #[test]
    // Test ListPlayerApplicationSettingValues response decoding success.
    fn test_list_player_application_setting_values_response_decode() {
        let response = [0x03, 0x03, 0x02, 0x04];
        let result = ListPlayerApplicationSettingValuesResponse::decode(&response);
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(result.num_player_application_setting_values, 3);
        assert_eq!(result.player_application_setting_value_ids, vec![0x03, 0x02, 0x04,]);
    }
}
