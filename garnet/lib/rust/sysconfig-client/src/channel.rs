// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{read_partition, write_partition, SysconfigPartition};
use serde_derive::{Deserialize, Serialize};
use thiserror::Error;
use zerocopy::{AsBytes, FromBytes, LayoutVerified};

const MAGIC: u32 = 0x5A799EA4;
const VERSION: u16 = 1;
const MAX_DATA_LENGTH: usize = 4080;
// Header length without checksum.
const HEADER_LENGTH: usize = std::mem::size_of::<Header>() - std::mem::size_of::<u32>();

// To be serialized and deserialized as raw bytes
#[repr(C)]
#[derive(Default, FromBytes, AsBytes)]
struct Header {
    magic: u32,           // must be MAGIC
    version: u16,         // must be 0x01
    data_len: u16,        // must be <= 4080 (u16 for ease of alignment)
    data_checksum: u32,   // CRC32 checksum of the data itself
    header_checksum: u32, // CRC32 of the header itself
}

impl Header {
    fn new(data: &[u8]) -> Result<Header, ChannelConfigError> {
        if data.len() > MAX_DATA_LENGTH {
            return Err(ChannelConfigError::DataLength(data.len() as u16));
        }
        let mut header = Header {
            magic: MAGIC,
            version: VERSION,
            data_len: data.len() as u16,
            data_checksum: crc::crc32::checksum_ieee(data),
            header_checksum: 0,
        };
        header.header_checksum = crc::crc32::checksum_ieee(&header.as_bytes()[..HEADER_LENGTH]);
        Ok(header)
    }
}

/// To be serialized as JSON:
#[derive(Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct OtaUpdateChannelConfig {
    channel_name: String, // chars must be in the range [32-126], up to 1024 long
    tuf_config_name: String,
}

impl OtaUpdateChannelConfig {
    pub fn new(
        channel_name: impl Into<String>,
        tuf_config_name: impl Into<String>,
    ) -> Result<OtaUpdateChannelConfig, ChannelConfigError> {
        let channel_name: String = channel_name.into();
        if channel_name.len() > 1024 {
            return Err(ChannelConfigError::ChannelNameLength(channel_name.len()));
        }
        // Cohort is limited to ASCII characters 32 to 126 (inclusive).
        // https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#attributes-3
        if channel_name.bytes().any(|b| b < 32 || b > 126) {
            return Err(ChannelConfigError::ChannelNameInvalid(channel_name));
        }
        Ok(OtaUpdateChannelConfig { channel_name, tuf_config_name: tuf_config_name.into() })
    }

    pub fn channel_name(&self) -> &str {
        &self.channel_name
    }

    pub fn tuf_config_name(&self) -> &str {
        &self.tuf_config_name
    }
}

#[derive(Debug, Error)]
pub enum ChannelConfigError {
    #[error("IO error: {:?}", _0)]
    IO(std::io::Error),

    #[error("JSON error: {:?}", _0)]
    JSON(serde_json::Error),

    #[error("Failed to parse header")]
    ParseHeader,

    #[error("Magic does not match: 0x{:x}", _0)]
    Magic(u32),

    #[error("Version does not match: {}", _0)]
    Version(u16),

    #[error("Header checksum does not match: 0x{:x}", _0)]
    HeaderChecksum(u32),

    #[error("Data length too large: {}", _0)]
    DataLength(u16),

    #[error("Data checksum does not match: 0x{:x}", _0)]
    DataChecksum(u32),

    #[error("Channel name too long: {} > 1024", _0)]
    ChannelNameLength(usize),

    #[error("Channel name contains invalid characters: {}", _0)]
    ChannelNameInvalid(String),
}

impl From<std::io::Error> for ChannelConfigError {
    fn from(e: std::io::Error) -> Self {
        ChannelConfigError::IO(e)
    }
}

impl From<serde_json::Error> for ChannelConfigError {
    fn from(e: serde_json::Error) -> Self {
        ChannelConfigError::JSON(e)
    }
}

/// Read the channel configuration from the config sub partition in sysconfig.
pub fn read_channel_config() -> Result<OtaUpdateChannelConfig, ChannelConfigError> {
    let partition = read_partition(SysconfigPartition::Config)?;
    let (header, data) = LayoutVerified::<&[u8], Header>::new_from_prefix(partition.as_slice())
        .ok_or(ChannelConfigError::ParseHeader)?;
    if header.magic != MAGIC {
        return Err(ChannelConfigError::Magic(header.magic));
    }
    if header.version != VERSION {
        return Err(ChannelConfigError::Version(header.version));
    }
    if header.header_checksum != crc::crc32::checksum_ieee(&header.bytes()[..HEADER_LENGTH]) {
        return Err(ChannelConfigError::HeaderChecksum(header.header_checksum));
    }
    if usize::from(header.data_len) > std::cmp::min(MAX_DATA_LENGTH, data.len()) {
        return Err(ChannelConfigError::DataLength(header.data_len));
    }
    let data = &data[..header.data_len.into()];
    if header.data_checksum != crc::crc32::checksum_ieee(data) {
        return Err(ChannelConfigError::DataChecksum(header.data_checksum));
    }
    let config: OtaUpdateChannelConfig = serde_json::from_slice(data)?;
    // Construct a new config to validate channel name.
    OtaUpdateChannelConfig::new(config.channel_name, config.tuf_config_name)
}

/// Write the channel configuration to the config sub partition in sysconfig.
pub fn write_channel_config(config: &OtaUpdateChannelConfig) -> Result<(), ChannelConfigError> {
    let mut data = serde_json::to_vec(config)?;
    let header = Header::new(&data)?;
    let mut partition = header.as_bytes().to_vec();
    partition.append(&mut data);
    write_partition(SysconfigPartition::Config, &partition)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sys_mock;
    use matches::assert_matches;

    #[test]
    fn test_read_channel_config() {
        let mut data = vec![
            0xa4, 0x9e, 0x79, 0x5a, // magic
            1, 0, // version
            55, 0, // data_len
            0xcc, 0xa3, 0xdd, 0xeb, // data_checksum
            0x4e, 0xf3, 0x5a, 0x57, // header_checksum
        ];
        let json = br#"{"channel_name":"some-channel","tuf_config_name":"tuf"}"#;
        data.extend_from_slice(json);
        data.resize(4096, 0);
        sys_mock::set_data(data);

        let config = OtaUpdateChannelConfig {
            channel_name: "some-channel".to_string(),
            tuf_config_name: "tuf".to_string(),
        };
        assert_eq!(read_channel_config().unwrap(), config);
    }

    #[test]
    fn test_read_channel_config_wrong_magic() {
        let data = vec![
            0xa5, 0x9e, 0x79, 0x5a, // magic
            1, 0, // version
            55, 0, // data_len
            0xcc, 0xa3, 0xdd, 0xeb, // data_checksum
            0x4e, 0xf3, 0x5a, 0x57, // header_checksum
        ];
        sys_mock::set_data(data);

        assert_matches!(read_channel_config(), Err(ChannelConfigError::Magic(0x5a799ea5)));
    }

    #[test]
    fn test_read_channel_config_wrong_version() {
        let data = vec![
            0xa4, 0x9e, 0x79, 0x5a, // magic
            2, 0, // version
            55, 0, // data_len
            0xcc, 0xa3, 0xdd, 0xeb, // data_checksum
            0xad, 0xf4, 0xd5, 0xd9, // header_checksum
        ];
        sys_mock::set_data(data);

        assert_matches!(read_channel_config(), Err(ChannelConfigError::Version(2)));
    }

    #[test]
    fn test_read_channel_config_wrong_header_checksum() {
        let data = vec![
            0xa4, 0x9e, 0x79, 0x5a, // magic
            1, 0, // version
            55, 0, // data_len
            0xcc, 0xa3, 0xdd, 0xeb, // data_checksum
            0x4f, 0xf3, 0x5a, 0x57, // header_checksum
        ];
        sys_mock::set_data(data);

        assert_matches!(read_channel_config(), Err(ChannelConfigError::HeaderChecksum(0x575af34f)));
    }

    #[test]
    fn test_read_channel_config_data_len_too_large() {
        let data = vec![
            0xa4, 0x9e, 0x79, 0x5a, // magic
            1, 0, // version
            55, 100, // data_len
            0xcc, 0xa3, 0xdd, 0xeb, // data_checksum
            0xc3, 0x22, 0xe8, 0x3b, // header_checksum
        ];
        sys_mock::set_data(data);

        assert_matches!(read_channel_config(), Err(ChannelConfigError::DataLength(25655)));
    }

    #[test]
    fn test_read_channel_config_wrong_data_checksum() {
        let mut data = vec![
            0xa4, 0x9e, 0x79, 0x5a, // magic
            1, 0, // version
            55, 0, // data_len
            0xcd, 0xa3, 0xdd, 0xeb, // data_checksum
            0x2b, 0x94, 0xe6, 0xef, // header_checksum
        ];
        let json = br#"{"channel_name":"some-channel","tuf_config_name":"tuf"}"#;
        data.extend_from_slice(json);
        sys_mock::set_data(data);

        assert_matches!(read_channel_config(), Err(ChannelConfigError::DataChecksum(0xebdda3cd)));
    }

    #[test]
    fn test_read_channel_config_invalid_json() {
        let mut data = vec![
            0xa4, 0x9e, 0x79, 0x5a, // magic
            1, 0, // version
            55, 0, // data_len
            0xac, 0x09, 0xd4, 0x27, // data_checksum
            0x29, 0xe9, 0x83, 0xfb, // header_checksum
        ];
        let json = br#"}"channel_name":"some-channel","tuf_config_name":"tuf"}"#;
        data.extend_from_slice(json);
        sys_mock::set_data(data);

        assert_matches!(read_channel_config(), Err(ChannelConfigError::JSON(_)));
    }

    #[test]
    fn test_write_channel_config() {
        let config = OtaUpdateChannelConfig {
            channel_name: "other-channel".to_string(),
            tuf_config_name: "other-tuf".to_string(),
        };
        write_channel_config(&config).unwrap();
        let mut expected = vec![
            0xa4, 0x9e, 0x79, 0x5a, // magic
            1, 0, // version
            62, 0, // data_len
            0x2b, 0x18, 0x6, 0xaa, // data_checksum
            0x24, 0xa7, 0xe1, 0x91, // header_checksum
        ];
        let expected_json = br#"{"channel_name":"other-channel","tuf_config_name":"other-tuf"}"#;
        expected.extend_from_slice(expected_json);
        expected.resize(4096, 0);

        assert_eq!(sys_mock::get_data(), expected);
    }

    #[test]
    fn test_write_channel_config_data_too_large() {
        let config = OtaUpdateChannelConfig {
            channel_name: "channel".repeat(1000),
            tuf_config_name: "tuf".to_string(),
        };
        assert_matches!(write_channel_config(&config), Err(ChannelConfigError::DataLength(_)));
    }

    #[test]
    fn test_ota_update_channel_config_new() {
        assert_eq!(
            OtaUpdateChannelConfig::new("channel", "tuf").unwrap(),
            OtaUpdateChannelConfig {
                channel_name: "channel".to_string(),
                tuf_config_name: "tuf".to_string(),
            }
        );
    }

    #[test]
    fn test_ota_update_channel_config_new_invalid_character() {
        assert_matches!(
            OtaUpdateChannelConfig::new("channel\n", "tuf"),
            Err(ChannelConfigError::ChannelNameInvalid(_))
        );
    }

    #[test]
    fn test_ota_update_channel_config_new_long_name() {
        assert_matches!(
            OtaUpdateChannelConfig::new(&"channel".repeat(200), "tuf"),
            Err(ChannelConfigError::ChannelNameLength(1400))
        );
    }
}
