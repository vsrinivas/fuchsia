// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::u8;

use super::*;

/// Packet format for a GetCurrentPlayerApplicationSettingValue command.
/// See AVRCP Sec 6.5.3
#[derive(Debug)]
pub struct GetCurrentPlayerApplicationSettingValueCommand {
    pub num_attribute_ids: u8,
    pub attribute_ids: Vec<PlayerApplicationSettingAttributeId>,
}

impl GetCurrentPlayerApplicationSettingValueCommand {
    pub fn new(
        attribute_ids: Vec<PlayerApplicationSettingAttributeId>,
    ) -> GetCurrentPlayerApplicationSettingValueCommand {
        let len: u8 = attribute_ids.len() as u8;
        Self { num_attribute_ids: len, attribute_ids }
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for GetCurrentPlayerApplicationSettingValueCommand {
    fn pdu_id(&self) -> PduId {
        PduId::GetCurrentPlayerApplicationSettingValue
    }
}

/// Specifies the AVC command type for this AVC command packet
impl VendorCommand for GetCurrentPlayerApplicationSettingValueCommand {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Status
    }
}

impl Decodable for GetCurrentPlayerApplicationSettingValueCommand {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessage);
        }

        let num_attribute_ids = buf[0];
        // There must be at least 1 attribute ID provided.
        // See AVRCP Sec 6.5.3
        if num_attribute_ids < 1 {
            return Err(Error::InvalidMessage);
        }
        let mut attribute_ids = vec![];
        let mut chunks = buf[1..].chunks(1);
        while let Some(chunk) = chunks.next() {
            attribute_ids.push(PlayerApplicationSettingAttributeId::try_from(chunk[0])?);
        }
        if attribute_ids.len() != num_attribute_ids as usize {
            return Err(Error::InvalidMessage);
        }
        Ok(Self { num_attribute_ids, attribute_ids })
    }
}

impl Encodable for GetCurrentPlayerApplicationSettingValueCommand {
    fn encoded_len(&self) -> usize {
        1 + self.num_attribute_ids as usize
    }
    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::InvalidMessageLength);
        }
        buf[0] = u8::from(self.num_attribute_ids);

        // There must be at least 1 attribute ID provided.
        // See AVRCP Sec 6.5.3
        if self.num_attribute_ids < 1 {
            return Err(Error::ParameterEncodingError);
        }

        if self.num_attribute_ids as usize != self.attribute_ids.len() {
            return Err(Error::ParameterEncodingError);
        }
        for (i, id) in self.attribute_ids.iter().enumerate() {
            buf[i + 1] = u8::from(id);
        }
        Ok(())
    }
}

/// Packet format for a GetCurrentPlayerApplicationSettingValue response.
/// See AVRCP Sec 6.5.3
#[derive(Debug)]
pub struct GetCurrentPlayerApplicationSettingValueResponse {
    num_values: u8,
    attribute_values: Vec<(PlayerApplicationSettingAttributeId, u8)>,
}

impl GetCurrentPlayerApplicationSettingValueResponse {
    #[allow(dead_code)]
    pub fn new(
        attribute_values: Vec<(PlayerApplicationSettingAttributeId, u8)>,
    ) -> GetCurrentPlayerApplicationSettingValueResponse {
        Self { num_values: attribute_values.len() as u8, attribute_values }
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for GetCurrentPlayerApplicationSettingValueResponse {
    fn pdu_id(&self) -> PduId {
        PduId::GetCurrentPlayerApplicationSettingValue
    }
}

impl Decodable for GetCurrentPlayerApplicationSettingValueResponse {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessage);
        }

        let num_values = buf[0];
        // There must be at least 1 attribute (ID,Value) pair provided.
        // See AVRCP Sec 6.5.3
        if num_values < 1 {
            return Err(Error::InvalidMessageLength);
        }

        let mut idx = 1;
        let mut attribute_values = vec![];
        while idx + 1 < buf.len() {
            let attribute_id: u8 = buf[idx];
            let value: u8 = buf[idx + 1];
            attribute_values
                .push((PlayerApplicationSettingAttributeId::try_from(attribute_id)?, value));
            idx += 2;
        }
        if attribute_values.len() != num_values as usize {
            return Err(Error::InvalidMessage);
        }
        Ok(Self { num_values, attribute_values })
    }
}

impl Encodable for GetCurrentPlayerApplicationSettingValueResponse {
    fn encoded_len(&self) -> usize {
        1 + 2 * self.num_values as usize
    }
    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::InvalidMessageLength);
        }
        if self.num_values as usize != self.attribute_values.len() {
            return Err(Error::ParameterEncodingError);
        }
        buf[0] = u8::from(self.num_values);
        let mut idx: usize = 0;
        let mut buf_idx: usize = 1;
        while idx < self.num_values as usize {
            let (id, val) = self.attribute_values[idx];
            buf[buf_idx] = u8::from(&id);
            buf[buf_idx + 1] = val;
            idx += 1;
            buf_idx += 2;
        }
        Ok(())
    }
}

impl From<PlayerApplicationSettings> for GetCurrentPlayerApplicationSettingValueResponse {
    fn from(src: PlayerApplicationSettings) -> GetCurrentPlayerApplicationSettingValueResponse {
        let mut values = vec![];

        if let Some(eq) = src.equalizer {
            values.push((PlayerApplicationSettingAttributeId::Equalizer, u8::from(&eq)))
        }
        if let Some(repeat_mode) = src.repeat_status_mode {
            values.push((
                PlayerApplicationSettingAttributeId::RepeatStatusMode,
                u8::from(&repeat_mode),
            ))
        }
        if let Some(shuffle_mode) = src.shuffle_mode {
            values.push((PlayerApplicationSettingAttributeId::ShuffleMode, u8::from(&shuffle_mode)))
        }
        if let Some(scan_mode) = src.scan_mode {
            values.push((PlayerApplicationSettingAttributeId::ScanMode, u8::from(&scan_mode)))
        }
        GetCurrentPlayerApplicationSettingValueResponse::new(values)
    }
}

impl TryFrom<GetCurrentPlayerApplicationSettingValueResponse> for PlayerApplicationSettings {
    type Error = Error;
    fn try_from(
        src: GetCurrentPlayerApplicationSettingValueResponse,
    ) -> Result<PlayerApplicationSettings, Error> {
        let mut setting = PlayerApplicationSettings::new(None, None, None, None);
        let mut idx: usize = 0;
        if src.num_values as usize != src.attribute_values.len() {
            return Err(Error::InvalidMessage);
        }
        while idx < src.num_values as usize {
            let (attribute_id, value) = src.attribute_values[idx];
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
            idx += 1;
        }
        Ok(setting)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::convert::TryInto;

    #[test]
    // Test GetCurrentPlayerApplicationSettingValue command encoding success.
    fn test_get_current_player_application_setting_value_command_encode() {
        let attribute_ids = vec![
            PlayerApplicationSettingAttributeId::Equalizer,
            PlayerApplicationSettingAttributeId::ScanMode,
        ];
        let command = GetCurrentPlayerApplicationSettingValueCommand::new(attribute_ids);
        assert_eq!(command.raw_pdu_id(), u8::from(&PduId::GetCurrentPlayerApplicationSettingValue));
        assert_eq!(command.command_type(), AvcCommandType::Status);
        assert_eq!(command.encoded_len(), 3);
        let mut buf = vec![0; command.encoded_len()];
        assert!(command.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, vec![0x02, 0x01, 0x04]);
    }

    #[test]
    // Test GetCurrentPlayerApplicationSettingValue command decoding success.
    fn test_get_current_player_application_setting_value_command_decode() {
        let command = [0x03, 0x04, 0x02, 0x03];
        let result = GetCurrentPlayerApplicationSettingValueCommand::decode(&command);
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(result.num_attribute_ids, 3);
        assert_eq!(
            result.attribute_ids,
            vec![
                PlayerApplicationSettingAttributeId::ScanMode,
                PlayerApplicationSettingAttributeId::RepeatStatusMode,
                PlayerApplicationSettingAttributeId::ShuffleMode,
            ]
        );
    }

    #[test]
    // Test GetCurrentPlayerApplicationSettingValue response encoding success.
    fn test_get_current_player_application_setting_value_response_encode() {
        let response_vals = vec![
            (PlayerApplicationSettingAttributeId::Equalizer, 0x02),
            (PlayerApplicationSettingAttributeId::RepeatStatusMode, 0x01),
            (PlayerApplicationSettingAttributeId::ShuffleMode, 0x01),
        ];
        let response = GetCurrentPlayerApplicationSettingValueResponse::new(response_vals);
        assert_eq!(
            response.raw_pdu_id(),
            u8::from(&PduId::GetCurrentPlayerApplicationSettingValue)
        );
        assert_eq!(response.encoded_len(), 7);
        let mut buf = vec![0; response.encoded_len()];
        assert!(response.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, vec![0x03, 0x01, 0x02, 0x02, 0x01, 0x03, 0x01]);
    }

    #[test]
    // Test GetCurrentPlayerApplicationSettingValue response decoding success.
    fn test_get_current_player_application_setting_value_response_decode() {
        let result = GetCurrentPlayerApplicationSettingValueResponse::decode(&[0x01, 0x04, 0x02]);
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(result.num_values, 1);
        assert_eq!(
            result.attribute_values,
            vec![(PlayerApplicationSettingAttributeId::ScanMode, 0x02)]
        );
    }

    #[test]
    // Test GetCurrentPlayerApplicationSettingValue response decoding error.
    fn test_get_current_player_application_setting_value_response_decode_error() {
        // Malformed result, contains two attributes, but missing second value.
        let result =
            GetCurrentPlayerApplicationSettingValueResponse::decode(&[0x02, 0x04, 0x02, 0x03]);
        assert!(result.is_err());
    }

    #[test]
    // Tests converting from GetCurrentPlayerApplicationSettingValue response to
    // FIDL PlayerApplicationSettings succeeds.
    fn test_get_current_player_application_setting_value_response_to_fidl_success() {
        let response_vals = vec![
            (PlayerApplicationSettingAttributeId::Equalizer, 0x02),
            (PlayerApplicationSettingAttributeId::RepeatStatusMode, 0x01),
            (PlayerApplicationSettingAttributeId::ShuffleMode, 0x01),
        ];
        let response = GetCurrentPlayerApplicationSettingValueResponse::new(response_vals);
        let settings: Result<PlayerApplicationSettings, Error> = response.try_into();
        assert!(settings.is_ok());
        let settings = settings.unwrap();
        assert_eq!(settings.repeat_status_mode, Some(RepeatStatusMode::Off));
        assert_eq!(settings.equalizer, Some(Equalizer::On));
        assert_eq!(settings.shuffle_mode, Some(ShuffleMode::Off));
        assert_eq!(settings.scan_mode, None);
    }

    #[test]
    // Tests converting from GetCurrentPlayerApplicationSettingValue response to
    // FIDL PlayerApplicationSettings errors, due to an invalid equalizer value.
    fn test_get_current_player_application_setting_value_response_to_fidl_error() {
        let response_vals = vec![
            // Invalid equalizer value.
            (PlayerApplicationSettingAttributeId::Equalizer, 0x03),
            (PlayerApplicationSettingAttributeId::RepeatStatusMode, 0x01),
            (PlayerApplicationSettingAttributeId::ShuffleMode, 0x01),
        ];
        let response = GetCurrentPlayerApplicationSettingValueResponse::new(response_vals);
        let settings: Result<PlayerApplicationSettings, Error> = response.try_into();
        assert!(settings.is_err());
    }
}
