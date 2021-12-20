// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_tpm_cr50::{PinWeaverError, HASH_SIZE};
use fuchsia_syslog::fx_log_warn;
use std::{convert::TryInto, marker::PhantomData};

use crate::{
    cr50::command::{Deserializable, Header, Serializable, Subcommand, TpmRequest},
    util::{DeserializeError, Deserializer, Serializer},
};

/// Pinweaver protocol version.
const PROTOCOL_VERSION: u8 = 1;

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
#[repr(u8)]
#[allow(dead_code)]
pub enum PinweaverMessageType {
    Invalid = 0,
    ResetTree = 1,
    InsertLeaf = 2,
    RemoveLeaf = 3,
    TryAuth = 4,
    ResetAuth = 5,
    GetLog = 6,
    LogReplay = 7,
}

/// Type for all pinweaver requests.
pub struct PinweaverRequest<Data, Response>
where
    Data: Serializable,
    Response: Deserializable,
{
    header: Header,
    version: u8,
    message_type: PinweaverMessageType,
    data: Data,
    _response: PhantomData<Response>,
}

impl<D, R> TpmRequest for PinweaverRequest<D, R>
where
    D: Serializable,
    R: Deserializable,
{
    type ResponseType = PinweaverResponse<R>;
}

impl<D, R> Serializable for PinweaverRequest<D, R>
where
    D: Serializable,
    R: Deserializable,
{
    fn serialize(&self, serializer: &mut Serializer) {
        self.header.serialize(serializer);
        serializer.put_u8(self.version);
        serializer.put_u8(self.message_type as u8);
        self.data.serialize(serializer);
    }
}

impl<D, R> PinweaverRequest<D, R>
where
    D: Serializable,
    R: Deserializable,
{
    pub fn new(message: PinweaverMessageType, data: D) -> PinweaverRequest<D, R> {
        PinweaverRequest {
            header: Header::new(Subcommand::Pinweaver),
            version: PROTOCOL_VERSION,
            message_type: message,
            data,
            _response: PhantomData,
        }
    }
}

pub struct PinweaverResponse<T>
where
    T: Deserializable,
{
    result_code: u32,
    pub root: [u8; HASH_SIZE as usize],
    pub data: Option<T>,
}

impl<T> Deserializable for PinweaverResponse<T>
where
    T: Deserializable,
{
    fn deserialize(deserializer: &mut Deserializer) -> Result<Self, DeserializeError> {
        let _ = Header::deserialize(deserializer)?;
        let version = deserializer.take_u8()?;
        if version != PROTOCOL_VERSION {
            fx_log_warn!("Unknown protocol version {}", version);
        }
        let _data_length = deserializer.take_le_u16()?;
        let response = PinweaverResponse {
            result_code: deserializer.take_le_u32()?,
            // take() will either return HASH_SIZE bytes or an error.
            root: deserializer.take(HASH_SIZE as usize)?.try_into().unwrap(),
            data: T::deserialize(deserializer).ok(),
        };

        Ok(response)
    }
}

impl<T> PinweaverResponse<T>
where
    T: Deserializable,
{
    pub fn ok(&self) -> Result<&Self, PinWeaverError> {
        if self.result_code == 0 {
            Ok(self)
        } else {
            // TODO(fxbug.dev/90618): figure out what should happen if pinweaver returns an error we
            // don't recognise.
            Err(PinWeaverError::from_primitive(self.result_code)
                .unwrap_or(PinWeaverError::VersionMismatch))
        }
    }
}

/// Data for PinweaverMessageType::ResetTree.
pub struct PinweaverResetTree {
    bits_per_level: u8,
    height: u8,
}

impl Serializable for PinweaverResetTree {
    fn serialize(&self, serializer: &mut Serializer) {
        serializer.put_le_u16(2);
        serializer.put_u8(self.bits_per_level);
        serializer.put_u8(self.height);
    }
}

impl PinweaverResetTree {
    pub fn new(bits_per_level: u8, height: u8) -> PinweaverRequest<Self, ()> {
        PinweaverRequest::new(
            PinweaverMessageType::ResetTree,
            PinweaverResetTree { bits_per_level, height },
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn test_reset_tree() {
        let mut serializer = Serializer::new();
        PinweaverResetTree::new(11, 10).serialize(&mut serializer);
        let array = serializer.into_vec();
        assert_eq!(
            array,
            vec![
                0x00, 0x25, /* Subcommand::Pinweaver */
                0x01, 0x01, /* Protocol version and message type */
                0x02, 0x00, /* Data length (little endian) */
                0x0b, 0x0a, /* Bits per level and height */
            ]
        )
    }

    #[test]
    fn test_reset_tree_response() {
        let data = vec![
            0x00, 0x25, /* Subcommand::Pinweaver */
            0x01, /* Protocol version */
            0x20, 0x00, /* Data length (little endian) */
            0x00, 0x00, 0x00, 0x00, /* Result code (little endian) */
            0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, /* Root hash 0-7   */
            0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01, /* Root hash 8-15  */
            0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, /* Root hash 16-23 */
            0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01, /* Root hash 24-31 */
        ];
        let mut deserializer = Deserializer::new(data);
        let response =
            PinweaverResponse::<()>::deserialize(&mut deserializer).expect("deserialize ok");
        assert_eq!(response.result_code, 0);
        assert_eq!(
            response.root,
            [
                0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45,
                0x23, 0x01, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xef, 0xcd, 0xab, 0x89,
                0x67, 0x45, 0x23, 0x01,
            ]
        );
    }
}
