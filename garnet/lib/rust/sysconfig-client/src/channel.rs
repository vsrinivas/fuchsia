// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::SysconfigError;
use serde::{Deserialize, Serialize};
use thiserror::Error;
use zerocopy::{AsBytes, FromBytes, LayoutVerified};

#[cfg(not(test))]
use crate::{read_sysconfig_partition, write_sysconfig_partition};

#[cfg(test)]
use lib_mock::*;

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
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
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
    #[error("Sysconfig Call Error: {:?}", _0)]
    SysconfigCall(SysconfigError),

    #[error("Sysconfig IO error: {:?}", _0)]
    SysconfigIO(std::io::Error),

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
        ChannelConfigError::SysconfigIO(e)
    }
}

impl From<serde_json::Error> for ChannelConfigError {
    fn from(e: serde_json::Error) -> Self {
        ChannelConfigError::JSON(e)
    }
}

impl From<SysconfigError> for ChannelConfigError {
    fn from(e: SysconfigError) -> Self {
        ChannelConfigError::SysconfigCall(e)
    }
}

/// Read the channel configuration from the config sub partition in sysconfig.
pub async fn read_channel_config() -> Result<OtaUpdateChannelConfig, ChannelConfigError> {
    let partition = read_sysconfig_partition().await?;
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
pub async fn write_channel_config(
    config: &OtaUpdateChannelConfig,
) -> Result<(), ChannelConfigError> {
    let mut data = serde_json::to_vec(config)?;
    let header = Header::new(&data)?;
    let mut partition = header.as_bytes().to_vec();
    partition.append(&mut data);
    write_sysconfig_partition(&partition).await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use matches::assert_matches;

    #[fasync::run_singlethreaded(test)]
    async fn test_read_channel_config() {
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
        lib_mock::set_data(data);

        let config = OtaUpdateChannelConfig {
            channel_name: "some-channel".to_string(),
            tuf_config_name: "tuf".to_string(),
        };
        assert_eq!(read_channel_config().await.unwrap(), config);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_channel_config_wrong_magic() {
        let data = vec![
            0xa5, 0x9e, 0x79, 0x5a, // magic
            1, 0, // version
            55, 0, // data_len
            0xcc, 0xa3, 0xdd, 0xeb, // data_checksum
            0x4e, 0xf3, 0x5a, 0x57, // header_checksum
        ];
        lib_mock::set_data(data);

        assert_matches!(read_channel_config().await, Err(ChannelConfigError::Magic(0x5a799ea5)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_channel_config_wrong_version() {
        let data = vec![
            0xa4, 0x9e, 0x79, 0x5a, // magic
            2, 0, // version
            55, 0, // data_len
            0xcc, 0xa3, 0xdd, 0xeb, // data_checksum
            0xad, 0xf4, 0xd5, 0xd9, // header_checksum
        ];
        lib_mock::set_data(data);

        assert_matches!(read_channel_config().await, Err(ChannelConfigError::Version(2)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_channel_config_wrong_header_checksum() {
        let data = vec![
            0xa4, 0x9e, 0x79, 0x5a, // magic
            1, 0, // version
            55, 0, // data_len
            0xcc, 0xa3, 0xdd, 0xeb, // data_checksum
            0x4f, 0xf3, 0x5a, 0x57, // header_checksum
        ];
        lib_mock::set_data(data);

        assert_matches!(
            read_channel_config().await,
            Err(ChannelConfigError::HeaderChecksum(0x575af34f))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_channel_config_data_len_too_large() {
        let data = vec![
            0xa4, 0x9e, 0x79, 0x5a, // magic
            1, 0, // version
            55, 100, // data_len
            0xcc, 0xa3, 0xdd, 0xeb, // data_checksum
            0xc3, 0x22, 0xe8, 0x3b, // header_checksum
        ];
        lib_mock::set_data(data);

        assert_matches!(read_channel_config().await, Err(ChannelConfigError::DataLength(25655)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_channel_config_wrong_data_checksum() {
        let mut data = vec![
            0xa4, 0x9e, 0x79, 0x5a, // magic
            1, 0, // version
            55, 0, // data_len
            0xcd, 0xa3, 0xdd, 0xeb, // data_checksum
            0x2b, 0x94, 0xe6, 0xef, // header_checksum
        ];
        let json = br#"{"channel_name":"some-channel","tuf_config_name":"tuf"}"#;
        data.extend_from_slice(json);
        lib_mock::set_data(data);

        assert_matches!(
            read_channel_config().await,
            Err(ChannelConfigError::DataChecksum(0xebdda3cd))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_channel_config_invalid_json() {
        let mut data = vec![
            0xa4, 0x9e, 0x79, 0x5a, // magic
            1, 0, // version
            55, 0, // data_len
            0xac, 0x09, 0xd4, 0x27, // data_checksum
            0x29, 0xe9, 0x83, 0xfb, // header_checksum
        ];
        let json = br#"}"channel_name":"some-channel","tuf_config_name":"tuf"}"#;
        data.extend_from_slice(json);
        lib_mock::set_data(data);

        assert_matches!(read_channel_config().await, Err(ChannelConfigError::JSON(_)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_channel_config() {
        let config = OtaUpdateChannelConfig {
            channel_name: "other-channel".to_string(),
            tuf_config_name: "other-tuf".to_string(),
        };
        write_channel_config(&config).await.unwrap();
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

        assert_eq!(lib_mock::get_data(), expected);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_channel_config_data_too_large() {
        let config = OtaUpdateChannelConfig {
            channel_name: "channel".repeat(1000),
            tuf_config_name: "tuf".to_string(),
        };
        assert_matches!(
            write_channel_config(&config).await,
            Err(ChannelConfigError::DataLength(_))
        );
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

#[cfg(test)]
mod lib_mock {
    use super::*;
    use std::cell::RefCell;
    thread_local!(pub static DATA: RefCell<Vec<u8>> = RefCell::new(vec![]));

    pub fn get_data() -> Vec<u8> {
        DATA.with(|data| data.borrow().clone())
    }

    pub fn set_data(new_data: Vec<u8>) {
        DATA.with(|data| *data.borrow_mut() = new_data);
    }

    pub async fn read_sysconfig_partition() -> Result<Vec<u8>, SysconfigError> {
        Ok(get_data())
    }

    pub async fn write_sysconfig_partition(data: &[u8]) -> Result<(), SysconfigError> {
        let ptn_size: usize = 4096;
        if data.len() > ptn_size as usize {
            return Err(SysconfigError::OutOfRange(data.len()));
        }
        let mut copy = data.clone().to_vec();
        copy.resize(ptn_size, 0);
        set_data(copy);
        Ok(())
    }
}
