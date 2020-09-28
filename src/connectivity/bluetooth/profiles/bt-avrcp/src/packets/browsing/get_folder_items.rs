// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_bluetooth_avrcp as fidl_avrcp, std::convert::TryInto};

use super::*;

/// The smallest packet size of a GetFolderItemsCommand.
/// 1 byte for scope, 4 for start_item, 4 for end_item, 1 for attribute_count.
pub const MIN_FOLDER_ITEMS_COMMAND_SIZE: usize = 10;

/// AVRCP 1.6.2 section 6.10.4.2 GetFolderItems
/// If `attribute_list` is None, then all attribute ids will be used. Otherwise,
/// the ids provided in the list are used.
#[derive(Debug)]
pub struct GetFolderItemsCommand {
    scope: Scope,
    start_item: u32,
    end_item: u32,
    attribute_list: Option<Vec<MediaAttributeId>>,
}

impl GetFolderItemsCommand {
    #[allow(unused)]
    pub fn new(
        scope: Scope,
        start_item: u32,
        end_item: u32,
        attribute_list: Option<Vec<MediaAttributeId>>,
    ) -> Self {
        Self { scope, start_item, end_item, attribute_list }
    }

    /// Returns the scope associated with the command.
    pub fn scope(&self) -> Scope {
        self.scope
    }
}

impl Decodable for GetFolderItemsCommand {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < MIN_FOLDER_ITEMS_COMMAND_SIZE {
            return Err(Error::InvalidMessage);
        }

        let scope = Scope::try_from(buf[0])?;

        // We currently don't support folder items for anything other than MediaPlayerList.
        if scope != Scope::MediaPlayerList {
            return Err(Error::InvalidParameter);
        }

        let start_item = u32::from_be_bytes(buf[1..5].try_into().unwrap());
        let end_item = u32::from_be_bytes(buf[5..9].try_into().unwrap());

        if start_item > end_item {
            return Err(Error::InvalidParameter);
        }

        let attribute_count = buf[9];

        let attribute_list = if attribute_count == 0x00 {
            // All attributes requested.
            None
        } else if attribute_count == 0xFF {
            // No attributes requested.
            Some(vec![])
        } else {
            let expected_buf_length =
                ATTRIBUTE_ID_LEN * (attribute_count as usize) + MIN_FOLDER_ITEMS_COMMAND_SIZE;

            if buf.len() < expected_buf_length {
                return Err(Error::InvalidMessage);
            }

            let mut attributes = vec![];
            let mut chunks = buf[10..].chunks_exact(4);
            while let Some(chunk) = chunks.next() {
                // As per AVRCP 1.6 Section 26, Appendix E, `MediaAttributeId`s are
                // only the lower byte of the 4 byte representation, so we can ignore
                // the upper 3 bytes.
                attributes.push(MediaAttributeId::try_from(chunk[3])?);
            }

            Some(attributes)
        };

        Ok(Self { scope, start_item, end_item, attribute_list })
    }
}

impl Encodable for GetFolderItemsCommand {
    fn encoded_len(&self) -> usize {
        MIN_FOLDER_ITEMS_COMMAND_SIZE
            + 4 * self.attribute_list.as_ref().map_or(MediaAttributeId::VARIANTS.len(), |a| a.len())
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::OutOfRange);
        }

        buf[0] = u8::from(&self.scope);
        // `self.start_item` is 4 bytes.
        buf[1..5].copy_from_slice(&self.start_item.to_be_bytes());
        // `self.end_item` is 4 bytes.
        buf[5..9].copy_from_slice(&self.end_item.to_be_bytes());

        let (num_attributes, attribute_list) = match &self.attribute_list {
            Some(x) if x.is_empty() => (0xff, vec![]), // No attributes.
            Some(x) => (u8::try_from(x.len()).map_err(|_| Error::OutOfRange)?, x.clone()),
            None => (0x00, MediaAttributeId::VARIANTS.to_vec()), // All attributes.
        };
        buf[9] = num_attributes;

        // Traverse the attribute list in chunks of 4 bytes (u32 size).
        // Copy the converted attribute ID into the 4 byte chunk in `buf`.
        for i in 0..attribute_list.len() {
            let id: u32 = u32::from(u8::from(&attribute_list[i]));
            let start_idx = MIN_FOLDER_ITEMS_COMMAND_SIZE + 4 * i;
            let end_idx = MIN_FOLDER_ITEMS_COMMAND_SIZE + 4 * (i + 1);

            buf[start_idx..end_idx].copy_from_slice(&id.to_be_bytes());
        }

        Ok(())
    }
}

/// The length of the header fields of a MediaPlayerItem.
/// The fields are: ItemType (1 byte), ItemLength (2 bytes).
/// Defined in AVRCP 1.6.2, Section 6.10.2.1.
pub const MEDIA_PLAYER_ITEM_HEADER_SIZE: usize = 3;

/// The smallest MediaPlayerItem size. Calculated by taking the number of bytes needed
/// to represent the mandatory (fixed sized) fields. This does not include the header fields:
/// ItemType and ItemLength.
/// Defined in AVRCP 1.6.2, Section 6.10.2.1.
pub const MIN_MEDIA_PLAYER_ITEM_SIZE: usize = 28;

/// The response parameters for a Media Player Item.
/// Defined in AVRCP 1.6.2, Section 6.10.2.1.
// TODO(fxbug.dev/45904): Maybe wrap major_player_type and player_sub_type into strongly typed variables.
#[derive(Clone, Debug, PartialEq)]
pub struct MediaPlayerItem {
    player_id: u16,
    major_player_type: u8,
    player_sub_type: u32,
    play_status: PlaybackStatus,
    feature_bit_mask: [u8; 16],
    charset_id: CharsetId,
    name: Vec<u8>,
}

impl Encodable for MediaPlayerItem {
    /// The size of `MediaPlayerItem` in bytes.
    /// Only variable length field is `name`, which can be calculated by taking
    /// the length of the array.
    fn encoded_len(&self) -> usize {
        MIN_MEDIA_PLAYER_ITEM_SIZE + self.name.len()
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::OutOfRange);
        }

        buf[0..2].copy_from_slice(&self.player_id.to_be_bytes());

        buf[2] = self.major_player_type;
        buf[3..7].copy_from_slice(&self.player_sub_type.to_be_bytes());
        buf[7] = u8::from(&self.play_status);

        buf[8..24].copy_from_slice(&self.feature_bit_mask);

        let charset_id = u16::from(&self.charset_id);
        buf[24..26].copy_from_slice(&charset_id.to_be_bytes());

        let name_length =
            u16::try_from(self.name.len()).map_err(|_| Error::ParameterEncodingError)?;
        buf[26..28].copy_from_slice(&name_length.to_be_bytes());

        // Copy the name at the end.
        buf[28..].copy_from_slice(&self.name[..]);

        Ok(())
    }
}

/// The FIDL MediaPlayerItem contains a subset of fields in the AVRCP MediaPlayerItem.
/// This is because the current Fuchsia MediaPlayer does not provide information for the
/// omitted fields. Consequently, this conversion populates the missing fields with
/// static response values.
///
/// The static response values are taken from AVRCP 1.6.2, Section 25.19.
impl From<fidl_avrcp::MediaPlayerItem> for MediaPlayerItem {
    fn from(src: fidl_avrcp::MediaPlayerItem) -> MediaPlayerItem {
        // The player_id should always be provided. If not, default to the error
        // case of player_id = 0.
        let player_id = src.player_id.unwrap_or(0);
        // Audio
        let major_player_type = 0x1;
        // No sub type
        let player_sub_type = 0x0;
        // The play_status should always be provided. If not, default to the error case.
        let play_status = src.playback_status.map(|s| s.into()).unwrap_or(PlaybackStatus::Error);
        // Arbitrary feature bitmask.
        let feature_bit_mask = [
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB7, 0x01, 0xEF, 0x02, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00,
        ];
        let charset_id = CharsetId::Utf8;
        // The displayable name should always be provided. If not, default to empty.
        let name = src.displayable_name.map(|s| s.into_bytes()).unwrap_or(vec![]);

        MediaPlayerItem {
            player_id,
            major_player_type,
            player_sub_type,
            play_status,
            feature_bit_mask,
            charset_id,
            name,
        }
    }
}

/// AVRCP 1.6.2 section 6.10.4.2 GetFolderItems
/// Currently, we only support Scope = MediaPlayerList. Therefore, the response packet
/// is formatted according to Section 6.10.2.1 from AVRCP 1.6.2.
// TODO(fxbug.dev/45904): Implement full FolderItemsResponse with other Browseable items defined
// in AVRCP 6.10.2.
#[derive(Debug)]
pub struct GetFolderItemsResponse {
    status: StatusCode,
    uid_counter: u16,
    item_list: Vec<MediaPlayerItem>,
}

impl GetFolderItemsResponse {
    #[allow(unused)]
    pub fn new(status: StatusCode, uid_counter: u16, item_list: Vec<MediaPlayerItem>) -> Self {
        Self { status, uid_counter, item_list }
    }
}

impl Encodable for GetFolderItemsResponse {
    /// 5 bytes for the fixed values: `status`, `uid_counter`, and `num_items`.
    /// Each item in `item_list` has variable size, so iterate and update total size.
    /// Each item in `item_list` also contains a header, which is not part of the object.
    fn encoded_len(&self) -> usize {
        let mut len: usize = 5;
        for item in &self.item_list {
            len += item.encoded_len() + MEDIA_PLAYER_ITEM_HEADER_SIZE;
        }

        len
    }

    // TODO(fxbug.dev/45904): Implement full encoding with other Browseable items defined
    // in AVRCP 6.10.2.
    // Currently, only encodes MediaPlayerList.
    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::BufferLengthOutOfRange);
        }

        buf[0] = u8::from(&self.status);
        buf[1..3].copy_from_slice(&self.uid_counter.to_be_bytes());

        let num_items =
            u16::try_from(self.item_list.len()).map_err(|_| Error::ParameterEncodingError)?;

        buf[3..5].copy_from_slice(&num_items.to_be_bytes());

        let mut idx = 5;
        for item in self.item_list.iter() {
            // MediaPlayerItem with no header.
            let item_size =
                u16::try_from(item.encoded_len()).map_err(|_| Error::ParameterEncodingError)?;
            let mut item_buf = vec![0; item.encoded_len()];
            item.encode(&mut item_buf[..])?;

            // Header: ItemType = MediaPlayerItem (0x01) from Section 6.10.2.1.
            let mut total_buf = vec![0x01];
            // Extend header by size of MediaPlayerItem payload.
            total_buf.extend_from_slice(&item_size.to_be_bytes());
            // Extend the header by the MediaPlayerItem payload.
            total_buf.extend_from_slice(&item_buf);

            // The total buffer size is the MediaPlayerItem plus the appended 3 byte header.
            let total_item_size = (item_size as usize) + MEDIA_PLAYER_ITEM_HEADER_SIZE;

            // Copy over the complete MediaPlayerItem to the input buf.
            buf[idx..idx + total_item_size].copy_from_slice(&total_buf[..]);

            idx += total_item_size as usize;
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl::encoding::Decodable as FidlDecodable;

    #[test]
    /// Encoding a GetFolderItemsCommand successfully produces a byte buffer.
    fn test_get_folder_items_command_encode() {
        let cmd = GetFolderItemsCommand::new(
            Scope::MediaPlayerList,
            1,
            4,
            Some(vec![MediaAttributeId::Title, MediaAttributeId::Genre]),
        );

        assert_eq!(cmd.encoded_len(), 18);
        let mut buf = vec![0; cmd.encoded_len()];
        assert_eq!(cmd.encode(&mut buf[..]).map_err(|e| format!("{:?}", e)), Ok(()));
        assert_eq!(
            buf,
            &[
                0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x02, 0x00, 0x00, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x06
            ]
        );
    }

    #[test]
    /// Sending expected buffer decodes successfully.
    fn test_get_folder_items_command_decode_success() {
        // `self.attribute_count` is zero, so all attributes are requested.
        let buf = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00];
        let cmd = GetFolderItemsCommand::decode(&buf[..]);
        assert!(cmd.is_ok());
        let cmd = cmd.expect("Just checked");
        assert_eq!(cmd.scope, Scope::MediaPlayerList);
        assert_eq!(cmd.start_item, 0);
        assert_eq!(cmd.end_item, 4);
        assert_eq!(cmd.attribute_list, None);

        // `self.attribute_count` is u8 max, so no attributes are requested.
        let buf = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xff];
        let cmd = GetFolderItemsCommand::decode(&buf[..]);
        assert!(cmd.is_ok());
        let cmd = cmd.expect("Just checked");
        assert_eq!(cmd.scope, Scope::MediaPlayerList);
        assert_eq!(cmd.start_item, 0);
        assert_eq!(cmd.end_item, 4);
        assert_eq!(cmd.attribute_list, Some(vec![]));

        // Normal case.
        let buf =
            [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x01];
        let cmd = GetFolderItemsCommand::decode(&buf[..]);
        assert!(cmd.is_ok());
        let cmd = cmd.expect("Just checked");
        assert_eq!(cmd.scope, Scope::MediaPlayerList);
        assert_eq!(cmd.start_item, 0);
        assert_eq!(cmd.end_item, 4);
        assert_eq!(cmd.attribute_list, Some(vec![MediaAttributeId::Title]));
    }

    #[test]
    /// Sending payloads that are malformed and/or contain invalid parameters should be
    /// gracefully handled.
    fn test_get_folder_items_command_decode_invalid_buf() {
        // Unsupported Scope provided in buffer.
        let invalid_scope_buf = [0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00];
        let cmd = GetFolderItemsCommand::decode(&invalid_scope_buf[..]);
        assert!(cmd.is_err());

        // Incomplete buffer.
        let invalid_format_buf = [0x00, 0x00, 0x00, 0x01];
        let cmd = GetFolderItemsCommand::decode(&invalid_format_buf[..]);
        assert!(cmd.is_err());

        // Attribute ID length and provided attribute IDs don't match up.
        let missing_buf = [
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x06, 0x00, 0x00, 0x00, 0x02,
            0x00, 0x00, 0x00, 0x03,
        ];
        let cmd = GetFolderItemsCommand::decode(&missing_buf[..]);
        assert!(cmd.is_err());

        // Invalid MediaAttributeId provided.
        let invalid_id_buf = [
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00, 0x00, 0x02,
            0x00, 0x00, 0x00, 0xa4,
        ];
        let cmd = GetFolderItemsCommand::decode(&invalid_id_buf[..]);
        assert!(cmd.is_err());
    }

    #[test]
    /// Tests encoding a response buffer for GetFolderItemsResponse works as intended.
    fn test_get_folder_items_response_encode() {
        let feature_bit_mask = [0; 16];
        let player_name = "Foobar".to_string().into_bytes();
        let item_list = vec![MediaPlayerItem {
            player_id: 5,
            major_player_type: 1,
            player_sub_type: 0,
            play_status: PlaybackStatus::Playing,
            feature_bit_mask,
            charset_id: CharsetId::Utf8,
            name: player_name,
        }];
        let response = GetFolderItemsResponse::new(StatusCode::InvalidParameter, 5, item_list);

        // 5 bytes for header, 3 bytes for MediaPlayerItem header, 34 bytes for MediaPlayerItem.
        assert_eq!(response.encoded_len(), 42);

        // Normal buffer.
        let mut buf = vec![0; response.encoded_len()];
        assert_eq!(response.encode(&mut buf[..]).map_err(|e| format!("{:?}", e)), Ok(()));

        // Have to split buffer check into two because Rust prevents formatting for arrays > 32 in length.
        let buf1 = [1, 0, 5, 0, 1, 1, 0, 34, 0, 5, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0];
        assert_eq!(buf[..24], buf1);
        let buf2 = [0, 0, 0, 0, 0, 0, 0, 0, 0, 106, 0, 6, 70, 111, 111, 98, 97, 114];
        assert_eq!(buf[24..], buf2);

        // Buffer that is too small.
        let mut buf = vec![0; 5];
        assert_eq!(
            response.encode(&mut buf[..]).map_err(|e| format!("{:?}", e)),
            Err("BufferLengthOutOfRange".to_string())
        );
    }

    #[test]
    /// Tests encoding a MediaPlayerItem succeeds.
    fn test_media_player_item() {
        let player_id = 10;
        let feature_bit_mask = [0; 16];
        let player_name = "Tea".to_string().into_bytes();
        let item = MediaPlayerItem {
            player_id,
            major_player_type: 1,
            player_sub_type: 0,
            play_status: PlaybackStatus::Playing,
            feature_bit_mask,
            charset_id: CharsetId::Utf8,
            name: player_name,
        };

        assert_eq!(item.encoded_len(), 31);
        let mut buf = vec![0; item.encoded_len()];
        assert_eq!(item.encode(&mut buf[..]).map_err(|e| format!("{:?}", e)), Ok(()));
        assert_eq!(
            buf,
            &[
                0, 10, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 106, 0,
                3, 84, 101, 97
            ]
        );
    }

    #[test]
    /// Tests converting from a FIDL MediaPlayerItem to a local MediaPlayerItem
    /// works as intended.
    fn test_fidl_to_media_player_item() {
        let player_id = 1;
        // Static value used in conversion.
        let feature_bit_mask = [
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB7, 0x01, 0xEF, 0x02, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00,
        ];
        let item_fidl = fidl_avrcp::MediaPlayerItem {
            player_id: Some(player_id),
            playback_status: Some(fidl_avrcp::PlaybackStatus::Stopped),
            displayable_name: Some("hi".to_string()),
            ..fidl_avrcp::MediaPlayerItem::new_empty()
        };
        let item: MediaPlayerItem = item_fidl.into();
        assert_eq!(item.player_id, player_id);
        assert_eq!(item.major_player_type, 1);
        assert_eq!(item.player_sub_type, 0);
        assert_eq!(item.feature_bit_mask, feature_bit_mask);
        assert_eq!(item.charset_id, CharsetId::Utf8);
        assert_eq!(item.name, "hi".to_string().as_bytes());
    }
}
