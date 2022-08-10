// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth_avrcp as fidl_avrcp,
    packet_encoding::{Decodable, Encodable},
    std::collections::HashSet,
    std::convert::{TryFrom, TryInto},
    tracing::{info, warn},
};

use crate::packets::{
    CharsetId, Error, FolderType, ItemType, MediaAttributeId, MediaType, PacketResult,
    PlaybackStatus, Scope, StatusCode, ATTRIBUTE_ID_LEN,
};

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
    /// The smallest packet size of a GetFolderItemsCommand.
    /// 1 byte for scope, 4 for start_item, 4 for end_item, 1 for attribute_count.
    pub const MIN_PACKET_SIZE: usize = 10;

    // Create a new command for getting media players.
    pub fn new_media_player_list(start_item: u32, end_item: u32) -> Self {
        Self { scope: Scope::MediaPlayerList, start_item, end_item, attribute_list: None }
    }

    // Create a new command for getting virtual file system.
    pub fn new_virtual_file_system(
        start_item: u32,
        end_item: u32,
        attr_option: fidl_avrcp::AttributeRequestOption,
    ) -> Self {
        Self {
            scope: Scope::MediaPlayerVirtualFilesystem,
            start_item,
            end_item,
            attribute_list: Self::attr_list_from_fidl(attr_option),
        }
    }

    // Create a new command for getting now playing list.
    pub fn new_now_playing_list(
        start_item: u32,
        end_item: u32,
        attr_option: fidl_avrcp::AttributeRequestOption,
    ) -> Self {
        Self {
            scope: Scope::NowPlaying,
            start_item,
            end_item,
            attribute_list: Self::attr_list_from_fidl(attr_option),
        }
    }

    fn attr_list_from_fidl(
        attr_option: fidl_avrcp::AttributeRequestOption,
    ) -> Option<Vec<MediaAttributeId>> {
        match attr_option {
            fidl_avrcp::AttributeRequestOption::GetAll(true) => None,
            fidl_avrcp::AttributeRequestOption::GetAll(false) => Some(vec![]),
            fidl_avrcp::AttributeRequestOption::AttributeList(attr_list) => {
                Some(attr_list.iter().map(Into::into).collect())
            }
        }
    }

    /// Returns the scope associated with the command.
    pub fn scope(&self) -> Scope {
        self.scope
    }

    #[cfg(test)]
    pub fn start_item(&self) -> u32 {
        self.start_item
    }

    #[cfg(test)]
    pub fn end_item(&self) -> u32 {
        self.end_item
    }

    #[cfg(test)]
    pub fn attribute_list(&self) -> Option<&Vec<MediaAttributeId>> {
        self.attribute_list.as_ref()
    }
}

impl Decodable for GetFolderItemsCommand {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < Self::MIN_PACKET_SIZE {
            return Err(Error::InvalidMessageLength);
        }

        let scope = Scope::try_from(buf[0])?;

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
                ATTRIBUTE_ID_LEN * (attribute_count as usize) + Self::MIN_PACKET_SIZE;

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
    type Error = Error;

    fn encoded_len(&self) -> usize {
        Self::MIN_PACKET_SIZE + 4 * self.attribute_list.as_ref().map_or(0, |a| a.len())
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
            None => (0x00, vec![]), // All attributes, attribute ID is omitted.
        };
        buf[9] = num_attributes;

        // Traverse the attribute list in chunks of 4 bytes (u32 size).
        // Copy the converted attribute ID into the 4 byte chunk in `buf`.
        for i in 0..attribute_list.len() {
            let id: u32 = u32::from(u8::from(&attribute_list[i]));
            let start_idx = Self::MIN_PACKET_SIZE + 4 * i;
            let end_idx = Self::MIN_PACKET_SIZE + 4 * (i + 1);

            buf[start_idx..end_idx].copy_from_slice(&id.to_be_bytes());
        }

        Ok(())
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum BrowseableItem {
    MediaPlayer(MediaPlayerItem),
    Folder(FolderItem),
    MediaElement(MediaElementItem),
}

impl BrowseableItem {
    /// The length of the header fields of a browsable item.
    /// The fields are: ItemType (1 byte), ItemLength (2 bytes).
    /// Defined in AVRCP 1.6.2, Section 6.10.2.
    const HEADER_SIZE: usize = 3;

    fn get_item_type(&self) -> ItemType {
        match self {
            Self::MediaPlayer(_) => ItemType::MediaPlayer,
            Self::Folder(_) => ItemType::Folder,
            Self::MediaElement(_) => ItemType::MediaElement,
        }
    }
}

impl Encodable for BrowseableItem {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        Self::HEADER_SIZE
            + match self {
                Self::MediaPlayer(m) => m.encoded_len(),
                Self::Folder(f) => f.encoded_len(),
                Self::MediaElement(e) => e.encoded_len(),
            }
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::OutOfRange);
        }

        // Encode item type.
        buf[0] = u8::from(&self.get_item_type());
        // Encode length of item in octets, not including header.
        let item_size = self.encoded_len() - Self::HEADER_SIZE;
        buf[1..3].copy_from_slice(&(item_size as u16).to_be_bytes());
        match self {
            Self::MediaPlayer(m) => {
                m.encode(&mut buf[3..])?;
            }
            Self::Folder(f) => {
                f.encode(&mut buf[3..])?;
            }
            Self::MediaElement(e) => {
                e.encode(&mut buf[3..])?;
            }
        }
        Ok(())
    }
}

impl Decodable for BrowseableItem {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < Self::HEADER_SIZE {
            return Err(Error::InvalidMessageLength);
        }

        let item_type = ItemType::try_from(buf[0])?;
        let item_size = u16::from_be_bytes(buf[1..3].try_into().unwrap());

        if buf.len() < Self::HEADER_SIZE + (item_size as usize) {
            return Err(Error::InvalidMessageLength);
        }

        let item = match item_type {
            ItemType::MediaPlayer => Self::MediaPlayer(MediaPlayerItem::decode(&buf[3..])?),
            ItemType::Folder => Self::Folder(FolderItem::decode(&buf[3..])?),
            ItemType::MediaElement => Self::MediaElement(MediaElementItem::decode(&buf[3..])?),
        };
        Ok(item)
    }
}

/// AVRCP 1.6.2 section 6.10.2.3.1 Attribute Value entry
#[derive(Clone, Debug, PartialEq)]
struct AttributeValueEntry {
    attribute_id: MediaAttributeId,
    value: String,
}

impl AttributeValueEntry {
    /// The smallest AttributeValueEntry size. Calculated by taking the number of bytes needed
    /// to represent the mandatory (fixed sized) fields.
    /// Attribute ID (4 bytes), char set ID (2 bytes), attr value len (2 bytes).
    /// Defined in AVRCP 1.6.2, Section 6.10.2.3.1.
    const MIN_PACKET_SIZE: usize = 8;
}

impl Encodable for AttributeValueEntry {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        // Only variable length field is `name`, which can be calculated by taking
        // the length of the array.
        Self::MIN_PACKET_SIZE + self.value.len()
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::OutOfRange);
        }

        let attr_id = u8::from(&self.attribute_id) as u32;
        buf[0..4].copy_from_slice(&attr_id.to_be_bytes());

        let charset_id = u16::from(&CharsetId::Utf8);
        buf[4..6].copy_from_slice(&charset_id.to_be_bytes());
        let val_len = self.value.len();
        if val_len > std::u16::MAX.into() {
            return Err(Error::InvalidMessageLength);
        }

        buf[6..8].copy_from_slice(&(val_len as u16).to_be_bytes());
        buf[8..8 + val_len].copy_from_slice(self.value.as_bytes());

        Ok(())
    }
}

impl Decodable for AttributeValueEntry {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < Self::MIN_PACKET_SIZE {
            return Err(Error::InvalidMessageLength);
        }

        let attr_id = u32::from_be_bytes(buf[0..4].try_into().unwrap());
        if attr_id > 0x8 {
            // AVRCP spec 1.6.2 Appendix E
            // Values 0x9-0xFFFFFFFF are reserved.
            return Err(Error::InvalidMessage);
        }
        // Since media attribute ID is u8, we only extract the lowest octet.
        let attribute_id = MediaAttributeId::try_from(buf[3])?;
        // TODO(fxdev.bug/100467): add support to appropriately convert non-utf8
        // charset ID value to utf8.
        let is_utf8 = is_utf8_charset_id(&buf[4..6].try_into().unwrap());

        let val_len = u16::from_be_bytes(buf[6..8].try_into().unwrap()) as usize;
        if buf.len() < Self::MIN_PACKET_SIZE + val_len {
            return Err(Error::InvalidMessageLength);
        }

        // TODO(fxdev.bug/100467): add support to appropriately convert non-utf8
        // charset ID folder names to utf8 names.
        let value = if is_utf8 {
            String::from_utf8(buf[Self::MIN_PACKET_SIZE..Self::MIN_PACKET_SIZE + val_len].to_vec())
                .or(Err(Error::ParameterEncodingError))?
        } else {
            "Unknown Value".to_string()
        };

        Ok(AttributeValueEntry { attribute_id, value })
    }
}

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
    name: String,
}

impl MediaPlayerItem {
    /// The smallest MediaPlayerItem size. Calculated by taking the number of bytes needed
    /// to represent the mandatory (fixed sized) fields. This does not include the header fields,
    /// ItemType and ItemLength. The fields are:
    /// Player ID (2 bytes), Media Player Type (1 byte), Player Sub Type (4 bytes),
    /// Play Status (1 byte), Feature Bit Mask (16 bytes), Character Set ID (2 bytes),
    /// Displayable Name Length (2 bytes).
    /// Defined in AVRCP 1.6.2, Section 6.10.2.1.
    const MIN_PAYLOAD_SIZE: usize = 28;
}

impl Encodable for MediaPlayerItem {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        // Only variable length field is `name`, which can be calculated by
        // taking the length of the array.
        Self::MIN_PAYLOAD_SIZE + self.name.len()
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

        let charset_id = u16::from(&CharsetId::Utf8);
        buf[24..26].copy_from_slice(&charset_id.to_be_bytes());

        let name_len = self.name.len();
        let name_length = u16::try_from(name_len).map_err(|_| Error::ParameterEncodingError)?;
        buf[26..28].copy_from_slice(&name_length.to_be_bytes());

        // Copy the name at the end.
        buf[28..28 + name_len].copy_from_slice(self.name.as_bytes());

        Ok(())
    }
}

/// TODO(fxb/102060): once we change CharsetId to be infalliable we can
/// change or remove this method.
fn is_utf8_charset_id(id_buf: &[u8; 2]) -> bool {
    let raw_val = u16::from_be_bytes(*id_buf);
    match CharsetId::try_from(raw_val) {
        Ok(id) => id.is_utf8(),
        Err(_) => {
            warn!("Unsupported charset ID {:?}", raw_val,);
            false
        }
    }
}

impl Decodable for MediaPlayerItem {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < Self::MIN_PAYLOAD_SIZE {
            return Err(Error::InvalidMessageLength);
        }

        let player_id = u16::from_be_bytes(buf[0..2].try_into().unwrap());
        let major_player_type = buf[2];
        let player_sub_type = u32::from_be_bytes(buf[3..7].try_into().unwrap());
        let play_status = PlaybackStatus::try_from(buf[7])?;

        let mut feature_bit_mask = [0; 16];
        feature_bit_mask.copy_from_slice(&buf[8..24]);
        let is_utf8 = is_utf8_charset_id(&buf[24..26].try_into().unwrap());

        let name_len = u16::from_be_bytes(buf[26..28].try_into().unwrap()) as usize;
        if buf.len() < Self::MIN_PAYLOAD_SIZE + name_len {
            return Err(Error::InvalidMessageLength);
        }
        // TODO(fxdev.bug/100467): add support to appropriately convert non-utf8
        // charset ID media player name to utf8 name.
        let name = if is_utf8 {
            String::from_utf8(
                buf[Self::MIN_PAYLOAD_SIZE..Self::MIN_PAYLOAD_SIZE + name_len].to_vec(),
            )
            .or(Err(Error::ParameterEncodingError))?
        } else {
            "Media Player".to_string()
        };

        Ok(MediaPlayerItem {
            player_id,
            major_player_type,
            player_sub_type,
            play_status,
            feature_bit_mask,
            name,
        })
    }
}

/// The response parameters for a Folder Item.
/// Defined in AVRCP 1.6.2, Section 6.10.2.2.
#[derive(Clone, Debug, PartialEq)]
pub struct FolderItem {
    folder_uid: u64,
    folder_type: FolderType,
    is_playable: bool,
    name: String,
}

impl FolderItem {
    /// The smallest FolderItem size. Calculated by taking the number of bytes needed
    /// to represent the mandatory (fixed sized) fields. This does not include the header fields,
    /// ItemType and ItemLength.
    /// Folder UID (8 bytes), Folder Type (1 byte), Is Playable (1 byte),
    /// Character Set ID (2 bytes), Displayable Name Length (2 bytes).
    /// Defined in AVRCP 1.6.2, Section 6.10.2.2.
    const MIN_PAYLOAD_SIZE: usize = 14;
}

impl Encodable for FolderItem {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        // Only variable length field is `name`, which can be calculated by
        // taking the length of the array.
        Self::MIN_PAYLOAD_SIZE + self.name.len()
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::OutOfRange);
        }

        buf[0..8].copy_from_slice(&self.folder_uid.to_be_bytes());
        buf[8] = u8::from(&self.folder_type);
        buf[9] = u8::from(self.is_playable);

        let charset_id = u16::from(&CharsetId::Utf8);
        buf[10..12].copy_from_slice(&charset_id.to_be_bytes());

        let name_len = self.name.len();
        let name_length = u16::try_from(name_len).map_err(|_| Error::ParameterEncodingError)?;
        buf[12..14].copy_from_slice(&name_length.to_be_bytes());

        // Copy the name at the end.
        buf[14..14 + name_len].copy_from_slice(self.name.as_bytes());

        Ok(())
    }
}

impl Decodable for FolderItem {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < Self::MIN_PAYLOAD_SIZE {
            return Err(Error::InvalidMessageLength);
        }

        let folder_uid = u64::from_be_bytes(buf[0..8].try_into().unwrap());
        let folder_type = FolderType::try_from(buf[8])?;
        if buf[9] > 1 {
            return Err(Error::OutOfRange);
        }
        let is_playable = buf[9] == 1;
        let is_utf8 = is_utf8_charset_id(&buf[10..12].try_into().unwrap());

        let name_len = u16::from_be_bytes(buf[12..14].try_into().unwrap()) as usize;
        if buf.len() < Self::MIN_PAYLOAD_SIZE + name_len {
            return Err(Error::InvalidMessageLength);
        }
        // TODO(fxdev.bug/100467): add support to appropriately convert non-utf8
        // charset ID folder name to utf8 name.
        let name = if is_utf8 {
            String::from_utf8(
                buf[Self::MIN_PAYLOAD_SIZE..Self::MIN_PAYLOAD_SIZE + name_len].to_vec(),
            )
            .or(Err(Error::ParameterEncodingError))?
        } else {
            "Folder".to_string()
        };

        Ok(FolderItem { folder_uid, folder_type, is_playable, name })
    }
}

/// The response parameters for a Folder Item.
/// Defined in AVRCP 1.6.2, Section 6.10.2.3.
#[derive(Clone, Debug, PartialEq)]
pub struct MediaElementItem {
    element_uid: u64,
    media_type: MediaType,
    name: String,
    attributes: Vec<AttributeValueEntry>,
}

impl MediaElementItem {
    /// The smallest MediaElementItem size. Calculated by taking the number of bytes needed
    /// to represent the mandatory (fixed sized) fields. This does not include the header fields,
    /// ItemType and ItemLength.
    /// Media Element UID (8 bytes), Media Type (1 byte), Character Set ID (2 bytes),
    /// Displayable Name Length (2 bytes), Number of Attributes (1 byte).
    /// Defined in AVRCP 1.6.2, Section 6.10.2.3.
    const MIN_PAYLOAD_SIZE: usize = 14;
}

impl Encodable for MediaElementItem {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        // Name and attributes are variable length.
        Self::MIN_PAYLOAD_SIZE
            + self.name.len()
            + self.attributes.iter().map(|attr| attr.encoded_len()).sum::<usize>()
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::OutOfRange);
        }

        buf[0..8].copy_from_slice(&self.element_uid.to_be_bytes());
        buf[8] = u8::from(&self.media_type);

        let charset_id = u16::from(&CharsetId::Utf8);
        buf[9..11].copy_from_slice(&charset_id.to_be_bytes());

        let name_len = self.name.len();
        let len = u16::try_from(name_len).map_err(|_| Error::ParameterEncodingError)?;
        buf[11..13].copy_from_slice(&len.to_be_bytes());
        buf[13..13 + name_len].copy_from_slice(self.name.as_bytes());

        buf[13 + name_len] = self.attributes.len() as u8;

        let mut next_idx = Self::MIN_PAYLOAD_SIZE + name_len; // after attribute length.
        for a in &self.attributes {
            a.encode(&mut buf[next_idx..next_idx + a.encoded_len()])?;
            next_idx += a.encoded_len();
        }

        Ok(())
    }
}

impl Decodable for MediaElementItem {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < Self::MIN_PAYLOAD_SIZE {
            return Err(Error::InvalidMessageLength);
        }

        let element_uid = u64::from_be_bytes(buf[0..8].try_into().unwrap());
        let media_type = MediaType::try_from(buf[8])?;

        let is_utf8 = is_utf8_charset_id(&buf[9..11].try_into().unwrap());

        let name_len = u16::from_be_bytes(buf[11..13].try_into().unwrap()) as usize;
        if buf.len() < 13 + name_len {
            return Err(Error::InvalidMessageLength);
        }

        // TODO(fxdev.bug/100467): add support to appropriately convert non-utf8
        // charset ID folder names to utf8 names.
        let name = if is_utf8 {
            String::from_utf8(buf[13..13 + name_len].to_vec())
                .or(Err(Error::ParameterEncodingError))?
        } else {
            "Media Element".to_string()
        };

        let mut next_idx = 13 + name_len;
        if buf.len() <= next_idx {
            return Err(Error::InvalidMessageLength);
        }
        let num_attrs = buf[next_idx];

        // Process all the attributes.
        next_idx += 1;
        let mut attributes = Vec::with_capacity(num_attrs.into());
        for _processed in 0..num_attrs {
            if buf.len() <= next_idx {
                return Err(Error::InvalidMessageLength);
            }
            let a = AttributeValueEntry::decode(&buf[next_idx..])?;
            next_idx += a.encoded_len();
            attributes.push(a);
        }

        Ok(MediaElementItem { element_uid, media_type, name, attributes })
    }
}

/// The FIDL MediaPlayerItem contains a subset of fields in the AVRCP MediaPlayerItem.
/// This is because the current Fuchsia MediaPlayer does not provide information for the
/// omitted fields. Consequently, this conversion populates the missing fields with
/// static response values.
///
/// The static response values are taken from AVRCP 1.6.2, Section 25.19.
impl From<fidl_avrcp::MediaPlayerItem> for BrowseableItem {
    fn from(src: fidl_avrcp::MediaPlayerItem) -> BrowseableItem {
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
            0x00, 0x00, 0x00, 0x00, 0x00, 0xB7, 0x01, 0xEF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00,
        ];
        // The displayable name should always be provided. If not, default to empty.
        let name = src.displayable_name.unwrap_or("".to_string());

        BrowseableItem::MediaPlayer(MediaPlayerItem {
            player_id,
            major_player_type,
            player_sub_type,
            play_status,
            feature_bit_mask,
            name,
        })
    }
}

impl TryFrom<BrowseableItem> for fidl_avrcp::MediaPlayerItem {
    type Error = fidl_avrcp::BrowseControllerError;

    fn try_from(src: BrowseableItem) -> Result<Self, Self::Error> {
        match src {
            BrowseableItem::MediaPlayer(p) => Ok(fidl_avrcp::MediaPlayerItem {
                player_id: Some(p.player_id),
                major_type: fidl_avrcp::MajorPlayerType::from_bits(p.major_player_type),
                sub_type: fidl_avrcp::PlayerSubType::from_bits(p.player_sub_type),
                playback_status: Some(p.play_status.into()),
                displayable_name: Some(p.name),
                ..fidl_avrcp::MediaPlayerItem::EMPTY
            }),
            _ => Err(fidl_avrcp::BrowseControllerError::PacketEncoding),
        }
    }
}

impl TryFrom<BrowseableItem> for fidl_avrcp::FileSystemItem {
    type Error = fidl_avrcp::BrowseControllerError;

    fn try_from(src: BrowseableItem) -> Result<Self, Self::Error> {
        match src {
            BrowseableItem::MediaElement(e) => {
                let mut attrs = fidl_avrcp::MediaAttributes::EMPTY;
                for attr in e.attributes {
                    match attr.attribute_id {
                        MediaAttributeId::Title => attrs.title = Some(attr.value),
                        MediaAttributeId::ArtistName => attrs.artist_name = Some(attr.value),
                        MediaAttributeId::AlbumName => attrs.album_name = Some(attr.value),
                        MediaAttributeId::TrackNumber => attrs.track_number = Some(attr.value),
                        MediaAttributeId::TotalNumberOfTracks => {
                            attrs.total_number_of_tracks = Some(attr.value)
                        }
                        MediaAttributeId::Genre => attrs.genre = Some(attr.value),
                        MediaAttributeId::PlayingTime => attrs.playing_time = Some(attr.value),
                        MediaAttributeId::DefaultCoverArt => {
                            info!("ignoring default cover art due to missing implementation")
                        }
                    };
                }
                Ok(Self::MediaElement(fidl_avrcp::MediaElementItem {
                    media_element_uid: Some(e.element_uid),
                    media_type: Some(e.media_type.into()),
                    displayable_name: Some(e.name.clone()),
                    attributes: Some(attrs),
                    ..fidl_avrcp::MediaElementItem::EMPTY
                }))
            }
            BrowseableItem::Folder(f) => Ok(Self::Folder(fidl_avrcp::FolderItem {
                folder_uid: Some(f.folder_uid),
                folder_type: Some(f.folder_type.into()),
                is_playable: Some(f.is_playable),
                displayable_name: Some(f.name),
                ..fidl_avrcp::FolderItem::EMPTY
            })),
            _ => Err(fidl_avrcp::BrowseControllerError::PacketEncoding),
        }
    }
}

/// AVRCP 1.6.2 section 6.9.3.2 SetBrowsedPlayer.
#[derive(Debug)]
pub enum GetFolderItemsResponse {
    Success(GetFolderItemsResponseParams),
    Failure(StatusCode),
}

/// AVRCP 1.6.2 section 6.10.4.2 GetFolderItems
/// Currently, we only support Scope = MediaPlayerList. Therefore, the response packet
/// is formatted according to Section 6.10.2.1 from AVRCP 1.6.2.
#[derive(Debug)]
pub struct GetFolderItemsResponseParams {
    uid_counter: u16,
    item_list: Vec<BrowseableItem>,
}

impl GetFolderItemsResponse {
    /// The packet size of a GetFolderItemsResponse that indicates failure.
    /// 1 byte for status
    const FAILURE_RESPONSE_SIZE: usize = 1;

    /// The smallest packet size of a GetFolderItemsResponse for success status.
    /// 1 byte for status, 2 for uid counter, 2 for number of items.
    const MIN_SUCCESS_RESPONSE_SIZE: usize = 5;

    pub fn new_success(uid_counter: u16, item_list: Vec<BrowseableItem>) -> Self {
        Self::Success(GetFolderItemsResponseParams { uid_counter, item_list })
    }

    #[cfg(test)]
    pub fn new_failure(status: StatusCode) -> Result<Self, Error> {
        if status == StatusCode::Success {
            return Err(Error::InvalidMessage);
        }
        Ok(Self::Failure(status))
    }
}

impl Encodable for GetFolderItemsResponse {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        match self {
            Self::Failure(_) => Self::FAILURE_RESPONSE_SIZE,
            Self::Success(r) => r.encoded_len(),
        }
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::BufferLengthOutOfRange);
        }

        match self {
            Self::Failure(status) => buf[0] = u8::from(status),
            Self::Success(r) => r.encode(&mut buf[..])?,
        };
        Ok(())
    }
}

impl Decodable for GetFolderItemsResponse {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        // Failure response size is the smallest valid message size.
        if buf.len() < Self::FAILURE_RESPONSE_SIZE {
            return Err(Error::InvalidMessageLength);
        }

        let status = StatusCode::try_from(buf[0])?;
        if status != StatusCode::Success {
            return Ok(Self::Failure(status));
        }
        Ok(Self::Success(GetFolderItemsResponseParams::decode(buf)?))
    }
}

impl GetFolderItemsResponseParams {
    // Returns a list of browseable items from a successful GetFolderItem response.
    pub fn item_list(self) -> Vec<BrowseableItem> {
        self.item_list
    }
}

impl Encodable for GetFolderItemsResponseParams {
    type Error = Error;

    /// 5 bytes for the fixed values: `status`, `uid_counter`, and `num_items`.
    /// Each item in `item_list` has variable size, so iterate and update total size.
    /// Each item in `item_list` also contains a header, which is not part of the object.
    fn encoded_len(&self) -> usize {
        GetFolderItemsResponse::MIN_SUCCESS_RESPONSE_SIZE
            + self.item_list.iter().map(|item| item.encoded_len()).sum::<usize>()
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::BufferLengthOutOfRange);
        }

        buf[0] = u8::from(&StatusCode::Success);
        buf[1..3].copy_from_slice(&self.uid_counter.to_be_bytes());

        let num_items =
            u16::try_from(self.item_list.len()).map_err(|_| Error::ParameterEncodingError)?;

        buf[3..5].copy_from_slice(&num_items.to_be_bytes());

        let mut idx = 5;
        for item in self.item_list.iter() {
            item.encode(&mut buf[idx..])?;
            idx += item.encoded_len();
        }

        Ok(())
    }
}

impl Decodable for GetFolderItemsResponseParams {
    type Error = Error;

    // Given a GetFolderItemsResponse message buf with supposed Success status,
    // it will try to decode the remaining response parameters.
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < GetFolderItemsResponse::MIN_SUCCESS_RESPONSE_SIZE {
            return Err(Error::InvalidMessageLength);
        }

        // Skip first byte since no need to process status as it would have
        // processed as part of SetBrowsedPlayerResponse.

        let uid_counter = u16::from_be_bytes(buf[1..3].try_into().unwrap());
        let num_items = u16::from_be_bytes(buf[3..5].try_into().unwrap());

        let mut next_idx = GetFolderItemsResponse::MIN_SUCCESS_RESPONSE_SIZE;
        let mut item_list = Vec::with_capacity(num_items.into());

        let mut seen_item_types = HashSet::new();

        for _processed in 0..num_items {
            if buf.len() <= next_idx {
                return Err(Error::InvalidMessageLength);
            }
            let a = BrowseableItem::decode(&buf[next_idx..])?;

            // According to AVRCP 1.6.2 section 6.10.1,
            // Media player item cannot exist with other items.
            let _ = seen_item_types.insert(a.get_item_type());
            if seen_item_types.contains(&ItemType::MediaPlayer) && seen_item_types.len() != 1 {
                return Err(Error::InvalidMessage);
            }

            next_idx += a.encoded_len();
            item_list.push(a);
        }

        if next_idx != buf.len() {
            return Err(Error::InvalidMessageLength);
        }

        Ok(GetFolderItemsResponseParams { uid_counter, item_list })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    /// Encoding a GetFolderItemsCommand successfully produces a byte buffer.
    fn test_get_folder_items_command_encode() {
        let cmd = GetFolderItemsCommand::new_media_player_list(1, 4);

        assert_eq!(cmd.encoded_len(), 10);
        let mut buf = vec![0; cmd.encoded_len()];
        let _ = cmd.encode(&mut buf[..]).expect("should be ok");
        assert_eq!(buf, &[0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00]);

        let cmd = GetFolderItemsCommand {
            scope: Scope::MediaPlayerVirtualFilesystem,
            start_item: 1,
            end_item: 4,
            attribute_list: Some(vec![MediaAttributeId::Title, MediaAttributeId::Genre]),
        };

        assert_eq!(cmd.encoded_len(), 18);
        let mut buf = vec![0; cmd.encoded_len()];
        let _ = cmd.encode(&mut buf[..]).expect("should be ok");
        assert_eq!(
            buf,
            &[
                0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x02, 0x00, 0x00, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x06
            ]
        );

        let cmd = GetFolderItemsCommand {
            scope: Scope::NowPlaying,
            start_item: 1,
            end_item: 4,
            attribute_list: Some(vec![]),
        };

        assert_eq!(cmd.encoded_len(), 10);
        let mut buf = vec![0; cmd.encoded_len()];
        let _ = cmd.encode(&mut buf[..]).expect("should be ok");
        assert_eq!(buf, &[0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0xFF,]);
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
    fn test_get_folder_items_response_empty_encode() {
        let response = GetFolderItemsResponse::new_failure(StatusCode::InvalidParameter)
            .expect("should have initialized");
        // 1 byte for status.
        assert_eq!(response.encoded_len(), 1);

        let mut buf = vec![0; response.encoded_len()];
        let _ = response.encode(&mut buf[..]).expect("should have succeeded");
        assert_eq!(buf, &[1]);

        // Buffer that is too small.
        let mut buf = vec![0; 0];
        let _ = response.encode(&mut buf[..]).expect_err("should have failed");

        // Invalid failure response.
        let _ = GetFolderItemsResponse::new_failure(StatusCode::Success)
            .expect_err("should have failed to initialize");
    }

    #[test]
    /// Tests encoding a response buffer for GetFolderItemsResponse works as intended.
    fn test_get_folder_items_response_media_player_list_encode() {
        let feature_bit_mask = [0; 16];
        let player_name = "Foobar".to_string();
        let item_list = vec![BrowseableItem::MediaPlayer(MediaPlayerItem {
            player_id: 5,
            major_player_type: 1,
            player_sub_type: 0,
            play_status: PlaybackStatus::Playing,
            feature_bit_mask,
            name: player_name,
        })];
        let response = GetFolderItemsResponse::new_success(5, item_list);

        // 5 bytes for header, 3 bytes for MediaPlayerItem header, 34 bytes for MediaPlayerItem.
        assert_eq!(response.encoded_len(), 42);

        // Normal buffer.
        let mut buf = vec![0; response.encoded_len()];
        let _ = response.encode(&mut buf[..]).expect("should have succeeded");

        // Have to split buffer check into two because Rust prevents formatting for arrays > 32 in length.
        let buf1 = [4, 0, 5, 0, 1, 1, 0, 34, 0, 5, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0];
        assert_eq!(buf[..24], buf1);
        let buf2 = [0, 0, 0, 0, 0, 0, 0, 0, 0, 106, 0, 6, 70, 111, 111, 98, 97, 114];
        assert_eq!(buf[24..], buf2);

        // Buffer that is too small.
        let mut buf = vec![0; 5];
        let _ = response.encode(&mut buf[..]).expect_err("should have failed");
    }

    #[test]
    /// Tests encoding a response buffer for GetFolderItemsResponse works as intended.
    fn test_get_folder_items_response_file_system_encode() {
        let item_list = vec![
            BrowseableItem::Folder(FolderItem {
                folder_uid: 1,
                folder_type: FolderType::Artists,
                is_playable: true,
                name: "Test".to_string(),
            }),
            BrowseableItem::MediaElement(MediaElementItem {
                element_uid: 1,
                media_type: MediaType::Video,
                name: "Test".to_string(),
                attributes: vec![
                    AttributeValueEntry {
                        attribute_id: MediaAttributeId::try_from(1)
                            .expect("should be attribute id"),
                        value: "1".to_string(),
                    },
                    AttributeValueEntry {
                        attribute_id: MediaAttributeId::try_from(2)
                            .expect("should be attribute id"),
                        value: "22".to_string(),
                    },
                ],
            }),
        ];
        let response = GetFolderItemsResponse::new_success(5, item_list);

        // 5 bytes for header, 3 bytes for FolderItem header, 18 bytes for FolderItem,
        // 3 bytes for MediaElementItem header, 37 bytes for MediaElementItem.
        assert_eq!(response.encoded_len(), 66);

        // Normal buffer.
        let mut buf = vec![0; response.encoded_len()];
        let _ = response.encode(&mut buf[..]).expect("should have succeeded");

        // Have to split buffer check into two because Rust prevents formatting for arrays > 32 in length.
        // Header.
        let buf1 = [4, 0, 5, 0, 2];
        assert_eq!(buf[..5], buf1);
        // FolderItem with header.
        let buf2 = [2, 0, 18, 0, 0, 0, 0, 0, 0, 0, 1, 3, 1, 0, 106, 0, 4, 0x54, 0x65, 0x73, 0x74];
        assert_eq!(buf[5..26], buf2);
        // MediaElementItem with header
        let buf3 = [
            3, 0, 37, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 106, 0, 4, 0x54, 0x65, 0x73, 0x74, 2, 0, 0, 0,
            1, 0, 106, 0, 1, 0x31, 0, 0, 0, 2, 0, 106, 0, 2, 0x32, 0x32,
        ];
        assert_eq!(buf[26..], buf3);

        // Buffer that is too small.
        let mut buf = vec![0; 5];
        let _ = response.encode(&mut buf[..]).expect_err("should have failed");
    }

    #[fuchsia::test]
    /// Sending expected buffer decodes successfully.
    fn test_get_folder_items_response_decode_success() {
        // With 1 media item.
        let buf = [
            4, 0, 1, 0, 1, // media player item begin
            1, 0, 32, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 1, 0,
            106, 0, 4, 0x74, 0x65, 0x73, 0x74,
        ];
        let response = GetFolderItemsResponse::decode(&buf).expect("Just checked");

        match response {
            GetFolderItemsResponse::Success(resp) => {
                assert_eq!(resp.uid_counter, 1);
                assert_eq!(resp.item_list.len(), 1);
                assert_eq!(
                    resp.item_list[0],
                    BrowseableItem::MediaPlayer(MediaPlayerItem {
                        player_id: 1,
                        major_player_type: 0,
                        player_sub_type: 1,
                        play_status: PlaybackStatus::Playing,
                        feature_bit_mask: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 1],
                        name: "test".to_string(),
                    })
                );
            }
            _ => panic!("should have been success response"),
        }

        // With no item.
        let buf = [4, 0, 1, 0, 0];
        let response = GetFolderItemsResponse::decode(&buf[..]).expect("should have decoded");
        match response {
            GetFolderItemsResponse::Success(resp) => {
                assert_eq!(resp.uid_counter, 1);
                assert_eq!(resp.item_list.len(), 0);
            }
            _ => panic!("should have been success response"),
        }

        // With failure response.
        let buf = [1];
        let resp = GetFolderItemsResponse::decode(&buf[..]).expect("should have decoded");
        match resp {
            GetFolderItemsResponse::Failure(status) => {
                assert_eq!(status, StatusCode::InvalidParameter);
            }
            _ => panic!("should have been failure response"),
        }
    }

    #[fuchsia::test]
    /// Sending expected buffer decodes successfully.
    fn test_get_folder_items_response_mixed_decode_success() {
        // With 1 folder item and 1 media element item.
        let buf = [
            4, 0, 1, 0, 2, // folder item begin
            2, 0, 18, 0, 0, 0, 0, 0, 0, 0, 1, 3, 1, 0, 106, 0, 4, 0x74, 0x65, 0x73, 0x74,
            // media element item begin
            3, 0, 29, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 106, 0, 3, 0x61, 0x62, 0x63, 1,
            // - media element attribute
            0, 0, 0, 1, 0, 106, 0, 4, 0x74, 0x65, 0x73, 0x74,
        ];
        let response = GetFolderItemsResponse::decode(&buf[..]).expect("should have decoded");
        match response {
            GetFolderItemsResponse::Success(resp) => {
                assert_eq!(resp.uid_counter, 1);
                assert_eq!(resp.item_list.len(), 2);
                assert_eq!(
                    resp.item_list[0],
                    BrowseableItem::Folder(FolderItem {
                        folder_uid: 1,
                        folder_type: FolderType::Artists,
                        is_playable: true,
                        name: "test".to_string(),
                    })
                );
                assert_eq!(
                    resp.item_list[1],
                    BrowseableItem::MediaElement(MediaElementItem {
                        element_uid: 1,
                        media_type: MediaType::Video,
                        name: "abc".to_string(),
                        attributes: vec![AttributeValueEntry {
                            attribute_id: MediaAttributeId::try_from(1)
                                .expect("should be attribute id"),
                            value: "test".to_string(),
                        },],
                    })
                );
            }
            _ => panic!("should have been success response"),
        }
    }

    #[fuchsia::test]
    /// Sending expected buffer decodes successfully.
    fn test_get_folder_items_response_decode_invalid_buf() {
        // Incomplete buffer.
        let invalid_format_buf = [4, 0, 1, 0];
        let _ = GetFolderItemsResponse::decode(&invalid_format_buf[..])
            .expect_err("should have failed");

        // Number of items does not match with provided items list.
        let missing_buf = [
            4, 0, 1, 0, 2, // folder item begin
            2, 0, 18, 0, 0, 0, 0, 0, 0, 0, 1, 3, 1, 0, 106, 0, 4, 0x74, 0x65, 0x73, 0x74,
        ];
        let _ = GetFolderItemsResponse::decode(&missing_buf[..]).expect_err("should have failed");

        // With inconsistent message length.
        let invalid_len_buf = [
            4, 0, 1, 0, 1, // media element item begin
            3, 0, 29, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 106, 0, 3, 0x61, 0x62, 0x63, 1,
            // - media element attribute.
            0, 0, 0, 1, 0, 106, 0, 3, 0x74, 0x65, 0x73, 0x74, // extra byte
        ];
        let _ =
            GetFolderItemsResponse::decode(&invalid_len_buf[..]).expect_err("should have failed");

        // With 1 folder item and 1 media player item.
        let invalid_items_buf = [
            4, 0, 1, 0, 2, // folder item begin
            2, 0, 18, 0, 0, 0, 0, 0, 0, 0, 1, 3, 1, 0, 106, 0, 4, 0x74, 0x65, 0x73, 0x74,
            // media player item begin
            1, 0, 32, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 1, 0,
            106, 0, 4, 0x74, 0x65, 0x73, 0x74,
        ];
        let _ =
            GetFolderItemsResponse::decode(&invalid_items_buf[..]).expect_err("should have failed");
    }

    #[test]
    /// Tests encoding a MediaPlayerItem succeeds.
    fn test_media_player_item_encode() {
        let player_id = 10;
        let feature_bit_mask = [0; 16];
        let player_name = "Tea".to_string();
        let item = MediaPlayerItem {
            player_id,
            major_player_type: 1,
            player_sub_type: 0,
            play_status: PlaybackStatus::Playing,
            feature_bit_mask,
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

    #[fuchsia::test]
    /// Tests decoding a MediaPlayerItem succeeds.
    fn test_media_player_item_decode_success() {
        // With utf8 name.
        let buf = [
            0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 1, 0, 106, 0, 4,
            0x74, 0x65, 0x73, 0x74,
        ];

        let item = MediaPlayerItem::decode(&buf[..]).expect("Just checked");
        assert_eq!(item.player_id, 1);
        assert_eq!(item.major_player_type, 0);
        assert_eq!(item.player_sub_type, 1);
        assert_eq!(item.play_status, PlaybackStatus::Playing);
        assert_eq!(item.feature_bit_mask, [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 1]);
        assert_eq!(item.name, "test".to_string());

        // Without utf8 name.
        let buf = [
            0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 1, 0, 5, 0, 4,
            0x74, 0x65, 0x73, 0x74,
        ];

        let item = MediaPlayerItem::decode(&buf[..]).expect("Just checked");
        assert_eq!(item.player_id, 1);
        assert_eq!(item.major_player_type, 0);
        assert_eq!(item.player_sub_type, 1);
        assert_eq!(item.play_status, PlaybackStatus::Playing);
        assert_eq!(item.feature_bit_mask, [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 1]);
        assert_eq!(item.name, "Media Player".to_string());
    }

    #[fuchsia::test]
    /// Tests decoding a MediaPlayerItem fails.
    fn test_media_player_item_decode_invalid_buf() {
        // Invalid buf that's too short.
        let invalid_buf = [0, 1, 0, 0];
        let _ = MediaPlayerItem::decode(&invalid_buf[..]).expect_err("should fail");

        // Invalid buf with mismatching name length field and actual name length.
        let invalid_buf = [
            0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 1, 0, 106, 0, 4,
            0x74, 0x65, 0x73,
        ];
        let _ = MediaPlayerItem::decode(&invalid_buf[..]).expect_err("should fail");
    }

    #[test]
    /// Tests encoding a FolderItem succeeds.
    fn test_folder_item_encode() {
        let folder_uid = 1;
        let folder_name = "Test".to_string();
        let item = FolderItem {
            folder_uid,
            folder_type: FolderType::Artists,
            is_playable: true,
            name: folder_name,
        };

        //  14 bytes of min payload size + 4 bytes for folder name.
        assert_eq!(item.encoded_len(), 18);
        let mut buf = vec![0; item.encoded_len()];
        assert_eq!(item.encode(&mut buf[..]).map_err(|e| format!("{:?}", e)), Ok(()));
        assert_eq!(buf, &[0, 0, 0, 0, 0, 0, 0, 1, 3, 1, 0, 106, 0, 4, 0x54, 0x65, 0x73, 0x74,]);
    }

    #[fuchsia::test]
    /// Tests decoding a FolderItem succeeds.
    fn test_folder_item_decode_success() {
        // With utf8 name.
        let buf = [0, 0, 0, 0, 0, 0, 0, 1, 3, 1, 0, 106, 0, 4, 0x74, 0x65, 0x73, 0x74];

        let item = FolderItem::decode(&buf[..]).expect("Just checked");
        assert_eq!(item.folder_uid, 1);
        assert_eq!(item.folder_type, FolderType::Artists);
        assert_eq!(item.is_playable, true);
        assert_eq!(item.name, "test".to_string());

        // Without utf8 name.
        let buf = [0, 0, 0, 0, 0, 0, 0, 1, 3, 1, 0, 5, 0, 4, 0x74, 0x65, 0x73, 0x74];

        let item = FolderItem::decode(&buf[..]).expect("Just checked");
        assert_eq!(item.folder_uid, 1);
        assert_eq!(item.folder_type, FolderType::Artists);
        assert_eq!(item.is_playable, true);
        assert_eq!(item.name, "Folder".to_string());
    }

    #[fuchsia::test]
    /// Tests decoding a FolderItem fails.
    fn test_folder_item_decode_invalid_buf() {
        // Invalid buf that's too short.
        let invalid_buf = [0, 1, 0, 0];
        let _ = FolderItem::decode(&invalid_buf[..]).expect_err("should fail");

        // Invalid buf with mismatching name length field and actual name length.
        let invalid_buf = [0, 0, 0, 0, 0, 0, 0, 1, 3, 1, 0, 106, 0, 2, 0x74];
        let _ = FolderItem::decode(&invalid_buf[..]).expect_err("should fail");
    }

    #[test]
    /// Tests encoding a MediaElementItem succeeds.
    fn test_media_element_item_encode() {
        let element_uid = 1;
        let element_name = "Test".to_string();
        let item = MediaElementItem {
            element_uid,
            media_type: MediaType::Video,
            name: element_name,
            attributes: vec![
                AttributeValueEntry {
                    attribute_id: MediaAttributeId::try_from(1).expect("should be attribute id"),
                    value: "1".to_string(),
                },
                AttributeValueEntry {
                    attribute_id: MediaAttributeId::try_from(2).expect("should be attribute id"),
                    value: "22".to_string(),
                },
            ],
        };

        // 14 bytes of min payload size + 4 bytes for element name +
        // 9 byte for first attr + 10 bytes for second attr.
        assert_eq!(item.encoded_len(), 37);
        let mut buf = vec![0; item.encoded_len()];
        assert_eq!(item.encode(&mut buf[..]).map_err(|e| format!("{:?}", e)), Ok(()));
        assert_eq!(
            buf,
            &[
                0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 106, 0, 4, 0x54, 0x65, 0x73, 0x74, 2, 0, 0, 0, 1, 0,
                106, 0, 1, 0x31, 0, 0, 0, 2, 0, 106, 0, 2, 0x32, 0x32,
            ]
        );
    }

    #[fuchsia::test]
    /// Tests decoding a MediaElementItem succeeds.
    fn test_media_element_item_decode_success() {
        let no_attrs_buf = [0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 3, 0, 3, 0x61, 0x62, 0x63, 0];

        let item = MediaElementItem::decode(&no_attrs_buf[..]).expect("Just checked");
        assert_eq!(item.element_uid, 1);
        assert_eq!(item.media_type, MediaType::Video);
        assert_eq!(item.name, "abc".to_string());
        assert_eq!(item.attributes.len(), 0);

        let attrs_buf = [
            0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 3, 0, 3, 0x61, 0x62, 0x63, 2, 0, 0, 0, 1, 0, 106, 0, 1,
            0x61, 0, 0, 0, 2, 0, 106, 0, 2, 0x62, 0x63,
        ];

        let item = MediaElementItem::decode(&attrs_buf[..]).expect("Just checked");
        assert_eq!(item.element_uid, 2);
        assert_eq!(item.media_type, MediaType::Audio);
        assert_eq!(item.name, "abc".to_string());
        assert_eq!(item.attributes.len(), 2);
        assert_eq!(
            item.attributes[0],
            AttributeValueEntry {
                attribute_id: MediaAttributeId::try_from(1).expect("should be attribute id"),
                value: "a".to_string(),
            }
        );
        assert_eq!(
            item.attributes[1],
            AttributeValueEntry {
                attribute_id: MediaAttributeId::try_from(2).expect("should be attribute id"),
                value: "bc".to_string(),
            }
        );
    }

    #[fuchsia::test]
    /// Tests decoding a MediaElementItem fails.
    fn test_media_element_item_decode_invalid_buf() {
        // Invalid buf that's too short.
        let invalid_buf = [0, 0, 0, 0, 0, 0, 0, 1];
        let _ = MediaElementItem::decode(&invalid_buf[..]).expect_err("should have failed");

        // Invalid buf with mismatching name length field and actual name length.
        let invalid_buf = [0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 106, 0, 2, 0x61, 0x62, 0x63, 0];
        let _ = MediaElementItem::decode(&invalid_buf[..]).expect_err("should have failed");

        // Invalid attribute ID was provided.
        let invalid_buf = [
            0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 106, 0, 3, 0x61, 0x62, 0x63, 1, 0, 0, 0, 9, 0, 106, 0, 4,
            0x74, 0x65, 0x73, 0x74,
        ];
        let _ = MediaElementItem::decode(&invalid_buf[..]).expect_err("should have failed");

        // Invalid number of attribute values were provided.
        let invalid_buf = [
            0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 106, 0, 3, 0x61, 0x62, 0x63, 2, 0, 0, 0, 1, 0, 106, 0, 4,
            0x74, 0x65, 0x73, 0x74,
        ];
        let _ = MediaElementItem::decode(&invalid_buf[..]).expect_err("should have failed");
    }

    #[test]
    /// Tests converting from a FIDL MediaPlayerItem to a local MediaPlayerItem
    /// works as intended.
    fn test_fidl_to_media_player_item() {
        let player_id = 1;
        // Static value used in conversion.
        let feature_bit_mask = [
            0x00, 0x00, 0x00, 0x00, 0x00, 0xB7, 0x01, 0xEF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00,
        ];
        let item_fidl = fidl_avrcp::MediaPlayerItem {
            player_id: Some(player_id),
            playback_status: Some(fidl_avrcp::PlaybackStatus::Stopped),
            displayable_name: Some("hi".to_string()),
            ..fidl_avrcp::MediaPlayerItem::EMPTY
        };
        let item: BrowseableItem = item_fidl.into();
        match item {
            BrowseableItem::MediaPlayer(item) => {
                assert_eq!(item.player_id, player_id);
                assert_eq!(item.major_player_type, 1);
                assert_eq!(item.player_sub_type, 0);
                assert_eq!(item.feature_bit_mask, feature_bit_mask);
                assert_eq!(item.name, "hi".to_string());
            }
            _ => panic!("expected MediaPlayer item"),
        }
    }
}
