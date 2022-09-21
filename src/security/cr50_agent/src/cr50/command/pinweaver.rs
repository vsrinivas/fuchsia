// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_tpm_cr50::{
    InsertLeafParams, LogReplayParams, PinWeaverError, RemoveLeafParams, TryAuthParams,
    DELAY_SCHEDULE_MAX_COUNT, HASH_SIZE, HE_SECRET_MAX_SIZE, LE_SECRET_MAX_SIZE, MAC_SIZE,
    MAX_LOG_ENTRIES,
};
use fuchsia_zircon as zx;
use num_derive::FromPrimitive;
use num_traits::FromPrimitive;
use std::{convert::TryInto, marker::PhantomData};
use tracing::warn;

use crate::{
    cr50::command::{Deserializable, Header, Serializable, Subcommand, TpmRequest},
    util::{DeserializeError, Deserializer, Serializer},
};

/// Pinweaver protocol version.
pub const PROTOCOL_VERSION: u8 = 1;

const PCR_CRITERIA_MAX: usize = 2;
const WRAP_BLOCK_SIZE: usize = 16;

#[derive(Debug, Copy, Clone, PartialEq, Eq, FromPrimitive)]
#[repr(u8)]
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
            warn!("Unknown protocol version {}", version);
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

#[derive(Copy, Clone, Default, Debug)]
pub struct DelayScheduleEntry {
    attempt_count: u32,
    time_diff: u32,
}

impl Serializable for DelayScheduleEntry {
    fn serialize(&self, serializer: &mut Serializer) {
        serializer.put_le_u32(self.attempt_count);
        serializer.put_le_u32(self.time_diff);
    }
}

#[derive(Copy, Clone, Default, Debug)]
pub struct ValidPcrValue {
    bitmask: [u8; 2],
    digest: [u8; HASH_SIZE as usize],
}

impl Serializable for ValidPcrValue {
    fn serialize(&self, serializer: &mut Serializer) {
        self.bitmask.as_slice().serialize(serializer);
        self.digest.as_slice().serialize(serializer);
    }
}

#[allow(dead_code)]
#[derive(Debug, Clone, PartialEq)]
/// Data used by the TPM to process a request.
pub struct UnimportedLeafData {
    /// Leaf minor version. Changes to this value add fields but cannot remove them. Newer values
    /// can be safely truncated to the most recent understood version.
    minor: u16,
    /// Leaf major version. Changes to this value can remove fields and unrecognised values should
    /// be rejected.
    major: u16,
    /// Length of pub_data in payload (requests only).
    pub_len: u16,
    /// Length of sec_data in payload (requests only).
    sec_len: u16,
    /// HMAC of all other fields (when in a response).
    /// HMAC of all other fields excluding payload (when in a request).
    pub hmac: [u8; MAC_SIZE as usize],
    iv: [u8; WRAP_BLOCK_SIZE],
    /// In a response, this is two arrays back-to-back:
    /// pub_data [u8; pub_len]
    /// cipher_text [u8; sec_len]
    ///
    /// In a request, this is the path hashes (h_aux).
    payload: Vec<u8>,
}

impl Serializable for UnimportedLeafData {
    fn serialize(&self, serializer: &mut Serializer) {
        serializer.put_le_u16(self.minor);
        serializer.put_le_u16(self.major);
        serializer.put_le_u16(self.pub_len);
        serializer.put_le_u16(self.sec_len);
        serializer.put(&self.hmac);
        serializer.put(&self.iv);
        serializer.put(&self.payload);
    }
}

impl Deserializable for UnimportedLeafData {
    fn deserialize(deserializer: &mut Deserializer) -> Result<Self, DeserializeError> {
        let mut data = UnimportedLeafData {
            minor: deserializer.take_le_u16()?,
            major: deserializer.take_le_u16()?,
            pub_len: deserializer.take_le_u16()?,
            sec_len: deserializer.take_le_u16()?,
            hmac: deserializer.take(MAC_SIZE as usize)?.try_into().unwrap(),
            iv: deserializer.take(WRAP_BLOCK_SIZE)?.try_into().unwrap(),
            payload: Vec::new(),
        };

        data.payload.extend_from_slice(deserializer.take(data.pub_len as usize)?);
        data.payload.extend_from_slice(deserializer.take(data.sec_len as usize)?);
        Ok(data)
    }
}

#[derive(Clone, Default, Debug)]
pub struct PinweaverInsertLeaf {
    label: u64,
    delay_schedule: [DelayScheduleEntry; DELAY_SCHEDULE_MAX_COUNT as usize],
    low_entropy_secret: [u8; LE_SECRET_MAX_SIZE as usize],
    high_entropy_secret: [u8; HE_SECRET_MAX_SIZE as usize],
    // Reset secret is a high-entropy secret.
    reset_secret: [u8; HE_SECRET_MAX_SIZE as usize],
    valid_pcr_criteria: [ValidPcrValue; PCR_CRITERIA_MAX],
    path_hashes: Vec<[u8; HASH_SIZE as usize]>,
}

pub struct PinweaverInsertLeafResponse {
    pub leaf_data: UnimportedLeafData,
}

impl Deserializable for PinweaverInsertLeafResponse {
    fn deserialize(deserializer: &mut Deserializer) -> Result<Self, DeserializeError> {
        Ok(PinweaverInsertLeafResponse {
            leaf_data: UnimportedLeafData::deserialize(deserializer)?,
        })
    }
}

impl Serializable for PinweaverInsertLeaf {
    fn serialize(&self, serializer: &mut Serializer) {
        // Size of PinweaverInsertLeaf is variable, so first we serialize the contents separately
        // and then we put the size and contents in the "real" serializer.
        let mut data_serializer = Serializer::new();
        data_serializer.put_le_u64(self.label);
        self.delay_schedule.as_slice().serialize(&mut data_serializer);
        self.low_entropy_secret.as_slice().serialize(&mut data_serializer);
        self.high_entropy_secret.as_slice().serialize(&mut data_serializer);
        self.reset_secret.as_slice().serialize(&mut data_serializer);
        self.valid_pcr_criteria.as_slice().serialize(&mut data_serializer);
        for item in self.path_hashes.iter() {
            item.as_slice().serialize(&mut data_serializer);
        }

        let data = data_serializer.into_vec();
        serializer.put_le_u16(data.len().try_into().unwrap());
        serializer.put(data.as_slice());
    }
}

fn delay_seconds_from_duration(duration: zx::Duration) -> Result<u32, PinWeaverError> {
    if duration == zx::Duration::INFINITE {
        // Treat an infinite duration as PW_BLOCK_ATTEMPTS, which is the max u32
        Ok(std::u32::MAX)
    } else {
        if duration <= zx::Duration::from_nanos(0) {
            // Negative and zero delay are invalid
            Err(PinWeaverError::DelayScheudleInvalid)
        } else if duration < zx::Duration::from_seconds(1) {
            // Round any duration less than a second up to a second
            Ok(1u32)
        } else {
            let duration_secs = duration.into_seconds();
            duration_secs.try_into().map_err(|_| PinWeaverError::DelayScheudleInvalid)
        }
    }
}

impl PinweaverInsertLeaf {
    pub fn new(
        params: InsertLeafParams,
    ) -> Result<PinweaverRequest<PinweaverInsertLeaf, PinweaverInsertLeafResponse>, PinWeaverError>
    {
        let mut data = PinweaverInsertLeaf::default();
        if let Some(label) = params.label {
            data.label = label;
        }
        if let Some(schedule) = params.delay_schedule {
            if schedule.len() > data.delay_schedule.len() {
                return Err(PinWeaverError::DelayScheudleInvalid);
            }
            let mut i = 0;
            for item in schedule.into_iter() {
                data.delay_schedule[i] = DelayScheduleEntry {
                    attempt_count: item.attempt_count,
                    time_diff: delay_seconds_from_duration(zx::Duration::from_nanos(
                        item.time_delay,
                    ))?,
                };
                i += 1;
            }
        }

        if let Some(le_secret) = params.le_secret {
            if le_secret.len() > data.low_entropy_secret.len() {
                return Err(PinWeaverError::LengthInvalid);
            }
            data.low_entropy_secret[0..le_secret.len()].copy_from_slice(&le_secret);
        }
        if let Some(he_secret) = params.he_secret {
            if he_secret.len() > data.high_entropy_secret.len() {
                return Err(PinWeaverError::LengthInvalid);
            }
            data.high_entropy_secret[0..he_secret.len()].copy_from_slice(&he_secret);
        }

        if let Some(reset_secret) = params.reset_secret {
            if reset_secret.len() > data.reset_secret.len() {
                return Err(PinWeaverError::LengthInvalid);
            }
            data.reset_secret[0..reset_secret.len()].copy_from_slice(&reset_secret);
        }

        if let Some(h_aux) = params.h_aux {
            data.path_hashes = h_aux;
        }

        let request = PinweaverRequest::new(PinweaverMessageType::InsertLeaf, data);

        Ok(request)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
/// Data for a TryAuth request.
/// https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/cr50/include/pinweaver_types.h;l=291;drc=1c95cff7463ef29bd0c6d087ce8c3d7e17f94c6a
pub struct PinweaverTryAuth {
    /// Low entropy secret that is going to be used to authenticate.
    low_entropy_secret: [u8; LE_SECRET_MAX_SIZE as usize],
    /// Wrapped leaf data.
    cred_metadata: Vec<u8>,
    /// Auxiliary hashes, from bottom left to top right.
    h_aux: Vec<u8>,
}

impl Serializable for PinweaverTryAuth {
    /// Serialization output:
    /// size of data [u16, little-endian]
    /// low-entropy secret [u8 array]
    /// wrapped leaf data [u8 array]
    /// auxiliary hashes [u8 array]
    fn serialize(&self, serializer: &mut Serializer) {
        let size = self.low_entropy_secret.len() + self.cred_metadata.len() + self.h_aux.len();
        serializer.put_le_u16(size.try_into().unwrap_or_else(|err| {
            warn!(?err, "TryAuth request too long!");
            0
        }));
        self.low_entropy_secret.as_slice().serialize(serializer);
        self.cred_metadata.as_slice().serialize(serializer);
        self.h_aux.as_slice().serialize(serializer);
    }
}

impl PinweaverTryAuth {
    pub fn new(
        params: TryAuthParams,
    ) -> Result<PinweaverRequest<PinweaverTryAuth, PinweaverTryAuthResponse>, PinWeaverError> {
        let data = PinweaverTryAuth {
            low_entropy_secret: params
                .le_secret
                .ok_or(PinWeaverError::TypeInvalid)?
                .try_into()
                .map_err(|_| PinWeaverError::LengthInvalid)?,
            cred_metadata: params.cred_metadata.ok_or(PinWeaverError::TypeInvalid)?,
            h_aux: params.h_aux.ok_or(PinWeaverError::TypeInvalid)?.into_iter().flatten().collect(),
        };

        Ok(PinweaverRequest::new(PinweaverMessageType::TryAuth, data))
    }
}

pub struct PinweaverTryAuthResponse {
    pub time_diff: u32,
    pub high_entropy_secret: [u8; HE_SECRET_MAX_SIZE as usize],
    pub reset_secret: [u8; HE_SECRET_MAX_SIZE as usize],
    pub unimported_leaf_data: UnimportedLeafData,
}

impl Deserializable for PinweaverTryAuthResponse {
    fn deserialize(deserializer: &mut Deserializer) -> Result<Self, DeserializeError> {
        Ok(PinweaverTryAuthResponse {
            time_diff: deserializer.take_le_u32()?,
            high_entropy_secret: deserializer
                .take(HE_SECRET_MAX_SIZE as usize)?
                .try_into()
                .unwrap(),
            reset_secret: deserializer.take(HE_SECRET_MAX_SIZE as usize)?.try_into().unwrap(),
            unimported_leaf_data: UnimportedLeafData::deserialize(deserializer)?,
        })
    }
}

#[derive(Debug, Clone, PartialEq)]
/// Remove a leaf.
/// https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/cr50/include/pinweaver_types.h;l=284;drc=1c95cff7463ef29bd0c6d087ce8c3d7e17f94c6a
pub struct PinweaverRemoveLeaf {
    /// Label of the leaf to remove.
    label: u64,
    /// HMAC used in merkle tree calculation.
    hmac: [u8; MAC_SIZE as usize],
    /// Auxiliary hashes, from bottom left to top right.
    h_aux: Vec<[u8; HASH_SIZE as usize]>,
}

impl Serializable for PinweaverRemoveLeaf {
    fn serialize(&self, serializer: &mut Serializer) {
        serializer.put_le_u16(
            (std::mem::size_of::<u64>()
                + MAC_SIZE as usize
                + (self.h_aux.len() * HASH_SIZE as usize))
                .try_into()
                .unwrap_or(0),
        );
        serializer.put_le_u64(self.label);
        serializer.put(&self.hmac);
        for item in self.h_aux.iter() {
            item.as_slice().serialize(serializer);
        }
    }
}

impl PinweaverRemoveLeaf {
    pub fn new(
        params: RemoveLeafParams,
    ) -> Result<PinweaverRequest<PinweaverRemoveLeaf, ()>, PinWeaverError> {
        Ok(PinweaverRequest::new(
            PinweaverMessageType::RemoveLeaf,
            PinweaverRemoveLeaf {
                label: params.label.ok_or(PinWeaverError::LabelInvalid)?,
                hmac: params.mac.ok_or(PinWeaverError::HmacAuthFailed)?,
                h_aux: params.h_aux.ok_or(PinWeaverError::PathAuthFailed)?,
            },
        ))
    }
}

#[derive(Debug, Clone, PartialEq)]
/// Data for a GetLog request.
/// https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/cr50/include/pinweaver_types.h;l=291;drc=1c95cff7463ef29bd0c6d087ce8c3d7e17f94c6a
pub struct PinweaverGetLog {
    /// Current root on the AP side.
    root: [u8; HASH_SIZE as usize],
}

impl PinweaverGetLog {
    pub fn new(
        root: [u8; HASH_SIZE as usize],
    ) -> Result<PinweaverRequest<PinweaverGetLog, PinweaverGetLogResponse>, PinWeaverError> {
        Ok(PinweaverRequest::new(PinweaverMessageType::GetLog, PinweaverGetLog { root }))
    }
}

impl Serializable for PinweaverGetLog {
    /// Serialization output:
    /// size of data [u16, little-endian]
    /// root hash [u8 array]
    fn serialize(&self, serializer: &mut Serializer) {
        serializer.put_le_u16(HASH_SIZE as u16);
        serializer.put(&self.root);
    }
}

#[derive(Debug, PartialEq)]
pub struct LeafActionReturnCode {
    /// Number of boots, used to track if Cr50 was rebooted since timer_value was last recorded.
    pub boot_count: u32,
    /// Seconds since boot.
    pub timer_value: u64,
    pub return_code: u32,
}

impl Deserializable for LeafActionReturnCode {
    fn deserialize(deserializer: &mut Deserializer) -> Result<Self, DeserializeError> {
        let ret = Ok(LeafActionReturnCode {
            boot_count: deserializer.take_le_u32()?,
            timer_value: deserializer.take_le_u64()?,
            return_code: deserializer.take_le_u32()?,
        });

        ret
    }
}

#[derive(Debug, PartialEq)]
pub enum GetLogEntryData {
    /// Contains leaf HMAC.
    InsertLeaf([u8; HASH_SIZE as usize]),
    RemoveLeaf(LeafActionReturnCode),
    TryAuth(LeafActionReturnCode),
    ResetTree,
}

impl Deserializable for GetLogEntryData {
    fn deserialize(deserializer: &mut Deserializer) -> Result<Self, DeserializeError> {
        // In C, this is a union, so we have to make sure we always take the full length of the
        // union. This is determined by its biggest member, which is the leaf HMAC in InsertLeaf.
        const DATA_SIZE: usize = HASH_SIZE as usize;
        let message_type = PinweaverMessageType::from_u8(deserializer.take_u8()?)
            .ok_or(DeserializeError::InvalidValue)?;

        let target = deserializer
            .available()
            .checked_sub(DATA_SIZE)
            .ok_or(DeserializeError::NotEnoughBytes)?;
        let ret = match message_type {
            PinweaverMessageType::InsertLeaf => GetLogEntryData::InsertLeaf(
                deserializer.take(HASH_SIZE as usize)?.try_into().unwrap(),
            ),
            PinweaverMessageType::RemoveLeaf => {
                GetLogEntryData::RemoveLeaf(LeafActionReturnCode::deserialize(deserializer)?)
            }
            PinweaverMessageType::TryAuth => {
                GetLogEntryData::TryAuth(LeafActionReturnCode::deserialize(deserializer)?)
            }
            PinweaverMessageType::ResetTree => GetLogEntryData::ResetTree,
            _ => return Err(DeserializeError::InvalidValue),
        };

        let available = deserializer.available();
        assert!(target <= available, "GetLogEntryData consumed too many bytes");

        // Take any remaining bytes.
        deserializer.take(available - target)?;

        Ok(ret)
    }
}

#[derive(Debug, PartialEq)]
pub struct PinweaverGetLogEntry {
    /// Root hash after this operation.
    pub root: [u8; HASH_SIZE as usize],
    /// Label of the leaf that was operated on.
    pub label: u64,
    /// Action that occurred.
    /// One of PW_INSERT_LEAF, PW_REMOVE_LEAF, PW_TRY_AUTH, or PW_RESET_AUTH.
    pub action: GetLogEntryData,
}

impl Deserializable for PinweaverGetLogEntry {
    fn deserialize(deserializer: &mut Deserializer) -> Result<Self, DeserializeError> {
        Ok(PinweaverGetLogEntry {
            root: deserializer.take(HASH_SIZE as usize)?.try_into().unwrap(),
            label: deserializer.take_le_u64()?,
            action: GetLogEntryData::deserialize(deserializer)?,
        })
    }
}

pub struct PinweaverGetLogResponse {
    pub log_entries: Vec<PinweaverGetLogEntry>,
}

impl Deserializable for PinweaverGetLogResponse {
    fn deserialize(deserializer: &mut Deserializer) -> Result<Self, DeserializeError> {
        let mut log_entries = Vec::with_capacity(MAX_LOG_ENTRIES as usize);

        while deserializer.available() > 0 && log_entries.len() < MAX_LOG_ENTRIES as usize {
            log_entries.push(PinweaverGetLogEntry::deserialize(deserializer)?);
        }

        Ok(PinweaverGetLogResponse { log_entries })
    }
}

/// Data for a LogReplay request.
/// https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/cr50/include/pinweaver_types.h;l=337;drc=1c95cff7463ef29bd0c6d087ce8c3d7e17f94c6a
pub struct PinweaverLogReplay {
    log_root: [u8; HASH_SIZE as usize],
    leaf_data: Vec<u8>,
    h_aux: Vec<u8>,
}

impl PinweaverLogReplay {
    pub fn new(
        params: LogReplayParams,
    ) -> Result<PinweaverRequest<PinweaverLogReplay, PinweaverLogReplayResponse>, PinWeaverError>
    {
        let data = PinweaverLogReplay {
            log_root: params.root_hash.ok_or(PinWeaverError::TypeInvalid)?,
            leaf_data: params.cred_metadata.ok_or(PinWeaverError::TypeInvalid)?,
            h_aux: params.h_aux.ok_or(PinWeaverError::TypeInvalid)?.into_iter().flatten().collect(),
        };

        Ok(PinweaverRequest::new(PinweaverMessageType::LogReplay, data))
    }
}

impl Serializable for PinweaverLogReplay {
    fn serialize(&self, serializer: &mut Serializer) {
        serializer.put_le_u16(
            (HASH_SIZE as usize + self.leaf_data.len() + self.h_aux.len())
                .try_into()
                .unwrap_or_else(|err| {
                    warn!(?err, "LogReplay request too long");
                    0
                }),
        );
        serializer.put(&self.log_root);
        self.leaf_data.as_slice().serialize(serializer);
        self.h_aux.as_slice().serialize(serializer);
    }
}

#[derive(Debug, PartialEq)]
pub struct PinweaverLogReplayResponse {
    pub unimported_leaf_data: UnimportedLeafData,
}

impl Deserializable for PinweaverLogReplayResponse {
    fn deserialize(deserializer: &mut Deserializer) -> Result<Self, DeserializeError> {
        Ok(PinweaverLogReplayResponse {
            unimported_leaf_data: UnimportedLeafData::deserialize(deserializer)?,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_tpm_cr50::DelayScheduleEntry as FidlDelayScheduleEntry;

    #[fuchsia::test]
    fn test_delay_seconds_from_duration() {
        let cases: Vec<(zx::Duration, Result<u32, PinWeaverError>)> = vec![
            (zx::Duration::INFINITE_PAST, Err(PinWeaverError::DelayScheudleInvalid)),
            (zx::Duration::from_seconds(-1), Err(PinWeaverError::DelayScheudleInvalid)),
            (zx::Duration::from_seconds(0), Err(PinWeaverError::DelayScheudleInvalid)),
            (zx::Duration::from_millis(500), Ok(1u32)),
            (zx::Duration::from_seconds(1), Ok(1u32)),
            (zx::Duration::from_millis(1500), Ok(1u32)),
            (zx::Duration::from_seconds(2), Ok(2u32)),
            (zx::Duration::from_seconds(20), Ok(20u32)),
            (zx::Duration::from_seconds(0xffffffff), Ok(0xffffffffu32)),
            (zx::Duration::from_seconds(0x100000000), Err(PinWeaverError::DelayScheudleInvalid)),
            (zx::Duration::INFINITE, Ok(0xffffffffu32)),
        ];

        for case in cases.iter() {
            assert_eq!(delay_seconds_from_duration(case.0), case.1);
        }
    }

    #[fuchsia::test]
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

    #[fuchsia::test]
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

    #[fuchsia::test]
    fn test_insert_leaf() {
        let le_secret: Vec<u8> = vec![0xaa; HASH_SIZE as usize];
        let he_secret: Vec<u8> = vec![0xbb; HASH_SIZE as usize];
        let reset_secret: Vec<u8> = vec![0xcc; HASH_SIZE as usize];
        let mut serializer = Serializer::new();
        let mut params = InsertLeafParams::EMPTY;
        params.label = Some(1);
        params.delay_schedule =
            Some(vec![FidlDelayScheduleEntry { attempt_count: 5, time_delay: 10_000_000_000 }]);
        params.le_secret = Some(le_secret.clone());
        params.he_secret = Some(he_secret.clone());
        params.reset_secret = Some(reset_secret.clone());
        params.h_aux = Some(vec![[0xab; HASH_SIZE as usize], [0xda; HASH_SIZE as usize]]);
        PinweaverInsertLeaf::new(params).expect("create insert leaf ok").serialize(&mut serializer);

        let vec = serializer.into_vec();
        assert_eq!(
            vec,
            vec![
                0x00, 0x25, /* Subcommand::Pinweaver */
                0x01, 0x02, /* Protocol version and message type */
                0x6c, 0x01, /* Data length (LE) */
                0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Label (LE), 8 bytes */
                /* Delay sched, eight bytes per value.  */
                0x05, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, // 0
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 1
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 2
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 3
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 4
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 5
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 6
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 7
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 8
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 9
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 10
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 11
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 12
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 13
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 14
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 15
                /* low entropy secret: 32 bytes */
                0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
                0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
                0xaa, 0xaa, 0xaa, 0xaa, //
                /* high entropy secret: 32 bytes */
                0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
                0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
                0xbb, 0xbb, 0xbb, 0xbb, //
                /* reset secret: 32 bytes */
                0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                0xcc, 0xcc, 0xcc, 0xcc, //
                /* valid pcr values: 2 * 34 bytes */
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 1
                /* h_aux: we provide 2*32 bytes */
                0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
                0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
                0xab, 0xab, 0xab, 0xab, //
                0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda,
                0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda,
                0xda, 0xda, 0xda, 0xda, //
            ]
        );
    }

    #[fuchsia::test]
    fn test_try_auth() {
        let mut serializer = Serializer::new();
        let mut params = TryAuthParams::EMPTY;
        params.le_secret = Some(vec![0xaa; LE_SECRET_MAX_SIZE as usize]);
        params.cred_metadata = Some(vec![0xbb; 32]); // artificially reduced to 32 bytes to make test readable.
        params.h_aux = Some(vec![[0xcc; HASH_SIZE as usize], [0xdd; HASH_SIZE as usize]]);
        PinweaverTryAuth::new(params).expect("create try auth ok").serialize(&mut serializer);

        let vec = serializer.into_vec();
        assert_eq!(
            vec,
            vec![
                0x00, 0x25, /* Subcommand::Pinweaver */
                0x01, 0x04, /* Protocol version and message type */
                0x80, 0x00, /* Data length (LE) */
                /* low entropy secret: 32 bytes */
                0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
                0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
                0xaa, 0xaa, 0xaa, 0xaa, //
                /* cred metadata: 32 bytes */
                0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
                0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
                0xbb, 0xbb, 0xbb, 0xbb, //
                /* h_aux: 2*32 bytes */
                0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                0xcc, 0xcc, 0xcc, 0xcc, //
                0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd,
                0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd,
                0xdd, 0xdd, 0xdd, 0xdd, //
            ]
        );
    }

    #[fuchsia::test]
    fn test_try_auth_response() {
        let data = vec![
            0x00, 0x25, /* Subcommand::Pinweaver */
            0x01, /* Protocol version */
            0x20, 0x00, /* Data length (little endian) */
            0x00, 0x00, 0x00, 0x00, /* Result code */
            /* Root hash (32 bytes) */
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
            0xaa, 0xaa, 0xaa, 0xaa, //
            0x01, 0x00, 0x00, 0x00, /* Time diff (LE) */
            /* high entropy secret: 32 bytes */
            0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
            0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
            0xbb, 0xbb, 0xbb, 0xbb, //
            /* reset secret: 32 bytes */
            0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
            0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
            0xcc, 0xcc, 0xcc, 0xcc, //
            /* leaf data */
            0x00, 0x00, 0x00, 0x00, /* major/minor (2 LE U16s) */
            0x01, 0x00, /* pub_len (LE) */
            0x02, 0x00, /* sec_len (LE) */
            /* HMAC: 32 bytes */
            0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
            0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
            0xcc, 0xcc, 0xcc, 0xcc, //
            /* IV: 16 bytes */
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
            0xaa, 0xaa, //
            /* payload (pub_len + sec_len) */
            0xaa, 0xbb, 0xcc,
        ];
        let mut deserializer = Deserializer::new(data);
        let response =
            PinweaverResponse::<PinweaverTryAuthResponse>::deserialize(&mut deserializer)
                .expect("deserialize ok");
        assert_eq!(response.result_code, 0);
        assert_eq!(
            response.root,
            [
                0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
                0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
                0xaa, 0xaa, 0xaa, 0xaa
            ]
        );
        let data = response.data.expect("parsed try auth response");
        assert_eq!(data.time_diff, 1);
        assert_eq!(
            data.high_entropy_secret,
            [
                0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
                0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
                0xbb, 0xbb, 0xbb, 0xbb,
            ]
        );
        assert_eq!(
            data.reset_secret,
            [
                0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                0xcc, 0xcc, 0xcc, 0xcc,
            ]
        );

        assert_eq!(data.unimported_leaf_data.major, 0);
        assert_eq!(data.unimported_leaf_data.minor, 0);
        assert_eq!(data.unimported_leaf_data.pub_len, 1);
        assert_eq!(data.unimported_leaf_data.sec_len, 2);
        assert_eq!(
            data.unimported_leaf_data.hmac,
            [
                0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                0xcc, 0xcc, 0xcc, 0xcc,
            ]
        );
        assert_eq!(
            data.unimported_leaf_data.iv,
            [
                0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
                0xaa, 0xaa,
            ]
        );
        assert_eq!(data.unimported_leaf_data.payload, vec![0xaa, 0xbb, 0xcc]);
    }

    #[fuchsia::test]
    fn test_remove_leaf() {
        let mut serializer = Serializer::new();
        let mut params = RemoveLeafParams::EMPTY;
        params.label = Some(0x11d1f);
        params.mac = Some([0xab; MAC_SIZE as usize]);
        params.h_aux = Some(vec![[0xcc; HASH_SIZE as usize], [0xab; HASH_SIZE as usize]]);

        PinweaverRemoveLeaf::new(params).expect("create remove leaf ok").serialize(&mut serializer);
        let vec = serializer.into_vec();
        assert_eq!(
            vec,
            vec![
                0x00, 0x25, /* Subcommand::Pinweaver */
                0x01, 0x03, /* Protocol version and message type */
                0x68, 0x00, /* Data length (LE) */
                /* Leaf label: 8 bytes */
                0x1f, 0x1d, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, //
                /* MAC: 32 bytes */
                0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
                0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
                0xab, 0xab, 0xab, 0xab, //
                /* h_aux: 2*32 bytes */
                0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                0xcc, 0xcc, 0xcc, 0xcc, //
                0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
                0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
                0xab, 0xab, 0xab, 0xab, //
            ]
        );
    }

    #[fuchsia::test]
    fn test_get_log() {
        let mut serializer = Serializer::new();
        let root = [0xaa; HASH_SIZE as usize];
        PinweaverGetLog::new(root).expect("create get log ok").serialize(&mut serializer);
        let vec = serializer.into_vec();
        assert_eq!(
            vec,
            vec![
                0x00, 0x25, /* Subcommand::Pinweaver */
                0x01, 0x06, /* Protocol version and message type */
                0x20, 0x00, /* Data length (LE) */
                /* Root hash: 32 bytes */
                0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
                0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
                0xaa, 0xaa, 0xaa, 0xaa,
            ]
        )
    }

    #[fuchsia::test]
    fn test_get_log_response() {
        let mut deserializer = Deserializer::new(vec![
            0x00, 0x25, /* Subcommand::Pinweaver */
            0x01, /* Protocol version */
            0x20, 0x00, /* Data length (little endian) */
            0x00, 0x00, 0x00, 0x00, /* Result code */
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
            0x01, 0x01, 0x01, 0x01, /* Root hash (in header) */
            /* Entry 1 */
            0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
            0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
            0xbb, 0xbb, 0xbb, 0xbb, /* Root hash - 32 bytes */
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, /* Label - 8 bytes */
            0x01, /* Type - RESET_TREE (1 byte) */
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, /* Padding */
            /* Entry 2 */
            0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc,
            0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc,
            0xbc, 0xbc, 0xbc, 0xbc, /* Root hash - 32 bytes */
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, /* Label - 8 bytes */
            0x02, /* Type - INSERT_LEAF (1 byte) */
            0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
            0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
            0xcc, 0xcc, 0xcc, 0xcc, /* leaf_hmac (32 bytes) */
        ]);

        let response = PinweaverResponse::<PinweaverGetLogResponse>::deserialize(&mut deserializer)
            .expect("deserialize ok");
        assert_eq!(response.result_code, 0);
        assert_eq!(response.root, [0x01; 32]);
        let entries = response.data.expect("parsed get log response").log_entries;
        assert_eq!(entries.len(), 2);
        assert_eq!(
            entries[0],
            PinweaverGetLogEntry {
                root: [0xbb; 32],
                label: 0xaaaaaaaaaaaaaaaa,
                action: GetLogEntryData::ResetTree,
            }
        );
        assert_eq!(
            entries[1],
            PinweaverGetLogEntry {
                root: [0xbc; 32],
                label: 0xaaaaaaaaaaaaaaaa,
                action: GetLogEntryData::InsertLeaf([0xcc; 32]),
            }
        )
    }

    #[fuchsia::test]
    fn test_log_replay() {
        let mut serializer = Serializer::new();
        let mut params = LogReplayParams::EMPTY;
        params.h_aux = Some(vec![[0xab; 32], [0xac; 32]]);
        params.root_hash = Some([0xcc; 32]);
        params.cred_metadata = Some(vec![0xdd; 8]);
        PinweaverLogReplay::new(params).expect("Create log reply ok").serialize(&mut serializer);

        assert_eq!(
            serializer.into_vec(),
            vec![
                0x00, 0x25, /* Subcommand::Pinweaver */
                0x01, 0x07, /* Protocol version and message type */
                0x68, 0x00, /* Data length (LE) */
                0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                0xcc, 0xcc, 0xcc, 0xcc, /* Root hash (32 bytes) */
                0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, /* Leaf data (8 bytes) */
                /* h_aux (2 * 32 bytes) */
                0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
                0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
                0xab, 0xab, 0xab, 0xab, /**/
                0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac,
                0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac, 0xac,
                0xac, 0xac, 0xac, 0xac,
            ]
        );
    }

    #[fuchsia::test]
    fn test_log_replay_response() {
        let mut deserializer = Deserializer::new(vec![
            0x00, 0x25, /* Subcommand::Pinweaver */
            0x01, /* Protocol version */
            0x20, 0x00, /* Data length (little endian) */
            0x00, 0x00, 0x00, 0x00, /* Result code */
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
            0x01, 0x01, 0x01, 0x01, /* Root hash (in header) */
            /* Unimported leaf data */
            0x01, 0x00, /* minor */
            0x00, 0x00, /* major */
            0x04, 0x00, /* pub_len */
            0x08, 0x00, /* sec_len */
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
            0xaa, 0xaa, 0xaa, 0xaa, /* HMAC (32 bytes) */
            0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
            0xcc, 0xcc, /* IV (16 bytes) */
            0x01, 0x01, 0x01, 0x01, /* pub_data (4 bytes) */
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, /* cipher_text (8 bytes) */
        ]);

        let response =
            PinweaverResponse::<PinweaverLogReplayResponse>::deserialize(&mut deserializer)
                .expect("deserialize ok");

        assert_eq!(response.result_code, 0);
        assert_eq!(response.root, [0x01; 32]);
        let leaf_data = response.data.expect("parsed log replay response").unimported_leaf_data;

        assert_eq!(leaf_data.minor, 0x01);
        assert_eq!(leaf_data.major, 0x00);
        assert_eq!(leaf_data.pub_len, 0x04);
        assert_eq!(leaf_data.sec_len, 0x08);
        assert_eq!(leaf_data.hmac, [0xaa; 32]);
        assert_eq!(leaf_data.iv, [0xcc; 16]);
        assert_eq!(leaf_data.payload, vec![0x1; 12]);
    }
}
