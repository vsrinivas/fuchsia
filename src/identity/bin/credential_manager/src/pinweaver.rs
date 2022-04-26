// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::label_generator::Label;
use async_trait::async_trait;
use fidl_fuchsia_identity_credential::{self as fcred, CredentialError};
use fidl_fuchsia_tpm_cr50::{self as fcr50, PinWeaverProxy};

#[cfg(test)]
use mockall::{automock, predicate::*};

pub type CredentialMetadata = Vec<u8>;
pub type Hash = [u8; fcr50::HASH_SIZE as usize];
pub type Mac = [u8; fcr50::MAC_SIZE as usize];

/// The PinWeaverProtocol provides an simple adapter between the
/// |fuchsia.identity.credential.CredentialManager| and
/// |fuchsia.tpm.cr50.PinWeaver| FIDL APIs.
#[cfg_attr(test, automock)]
#[async_trait]
pub trait PinWeaverProtocol {
    /// Creates an empty Merkle tree with |bits_per_level| and |height|.
    /// On Success, Returns the |root_hash| of the empty tree within the given
    /// parameters.
    async fn reset_tree(&self, bits_per_level: u8, height: u8) -> Result<Hash, CredentialError>;

    /// Inserts a new credential represented by its |label| and |h_aux|
    /// returning a Mac and CredentialMetadata.
    async fn insert_leaf(
        &self,
        label: &Label,
        h_aux: Vec<Hash>,
        params: &fcred::AddCredentialParams,
    ) -> Result<(Mac, CredentialMetadata), CredentialError>;

    /// Attempts to remove a leaf on the merkle tree. On success nothing
    /// is returned otherwise an appropriate error is returned.
    async fn remove_leaf(
        &self,
        label: &Label,
        mac: Hash,
        h_aux: Vec<Hash>,
    ) -> Result<(), CredentialError>;

    /// Attempts to authenticate a leaf of the merkle tree. On success the
    /// HeSecret is returned otherwise an appropriate error is returned.
    async fn try_auth(
        &self,
        le_secret: &Vec<u8>,
        h_aux: Vec<Hash>,
        cred_metadata: CredentialMetadata,
    ) -> Result<fcr50::TryAuthResponse, CredentialError>;

    /// Retrieves the set of replay logs starting from the specified root hash.
    /// If Found: Returns all log entries including and starting from the
    /// operation specified by the root hash parameter.
    /// If Not Found: Returns all known log entries.
    async fn get_log(&self, root_hash: &Hash) -> Result<Vec<fcr50::LogEntry>, CredentialError>;

    /// Applies a TryAuth operation replay log by modifying the credential
    /// metadata based on the state of the replay log.
    /// This will step forward any credential metadata for the appropriate
    /// label, whether or not it matches the exact state in history.
    /// On Success: Returns the updated leaf hmac and credential metadata.
    /// On Failure: Returns an error.
    async fn log_replay(
        &self,
        root_hash: Hash,
        h_aux: Vec<Hash>,
        cred_metadata: CredentialMetadata,
    ) -> Result<fcr50::LogReplayResponse, CredentialError>;
}

pub struct PinWeaver {
    proxy: PinWeaverProxy,
}

impl PinWeaver {
    /// Constructs a new |PinWeaverProtocol| provided a |PinWeaverProxy|.
    pub fn new(proxy: PinWeaverProxy) -> Self {
        Self { proxy }
    }
}

#[async_trait]
impl PinWeaverProtocol for PinWeaver {
    /// Calls |PinWeaverProxy| to reset the tree with the provided
    /// |bits_per_level| and |height|. Maps |PinWeaverErrors| to
    /// |CredentialError::InternalError|.
    async fn reset_tree(&self, bits_per_level: u8, height: u8) -> Result<Hash, CredentialError> {
        self.proxy
            .reset_tree(bits_per_level, height)
            .await
            .map_err(|_| CredentialError::InternalError)?
            .map_err(|_| CredentialError::InternalError)
    }

    /// Converts the |fcred::AddCredentialParams| into its corresponding
    /// |fcr50::InsertLeafParams| type, calls the |PinWeaverProxy| and converts
    /// the results into |CredentialErrors|.
    async fn insert_leaf(
        &self,
        label: &Label,
        h_aux: Vec<Hash>,
        params: &fcred::AddCredentialParams,
    ) -> Result<(Mac, CredentialMetadata), CredentialError> {
        let insert_leaf_params = fcr50::InsertLeafParams {
            label: Some(label.value()),
            h_aux: Some(h_aux),
            le_secret: params.le_secret.clone(),
            he_secret: params.he_secret.clone(),
            reset_secret: params.reset_secret.clone(),
            delay_schedule: Some(convert_delay_schedule(&params.delay_schedule)?),
            ..fcr50::InsertLeafParams::EMPTY
        };
        let response = self
            .proxy
            .insert_leaf(insert_leaf_params)
            .await
            .map_err(|_| CredentialError::InternalError)?
            .map_err(|_| CredentialError::InternalError)?;
        let mac = response.mac.ok_or(CredentialError::InternalError)?;
        let cred_metadata = response.cred_metadata.ok_or(CredentialError::InternalError)?;
        Ok((mac, cred_metadata))
    }

    /// Attempts to remove a leaf on the merkle tree specified by |label|.
    /// On success nothing is returned otherwise an appropriate error is
    /// returned.
    async fn remove_leaf(
        &self,
        label: &Label,
        mac: Hash,
        h_aux: Vec<Hash>,
    ) -> Result<(), CredentialError> {
        let remove_leaf_params = fcr50::RemoveLeafParams {
            label: Some(label.value()),
            mac: Some(mac),
            h_aux: Some(h_aux),
            ..fcr50::RemoveLeafParams::EMPTY
        };
        self.proxy
            .remove_leaf(remove_leaf_params)
            .await
            .map_err(|_| CredentialError::InternalError)?
            .map_err(|_| CredentialError::InternalError)?;
        Ok(())
    }

    /// Simply inserts |le_secret|, |h_aux| and |cred_metadata| into |TryAuthParams|
    /// and returns the resulting |TryAuthResponse|.
    async fn try_auth(
        &self,
        le_secret: &Vec<u8>,
        h_aux: Vec<Hash>,
        cred_metadata: CredentialMetadata,
    ) -> Result<fcr50::TryAuthResponse, CredentialError> {
        let try_auth_params = fcr50::TryAuthParams {
            le_secret: Some(le_secret.clone()),
            h_aux: Some(h_aux),
            cred_metadata: Some(cred_metadata),
            ..fcr50::TryAuthParams::EMPTY
        };
        let response = self
            .proxy
            .try_auth(try_auth_params)
            .await
            .map_err(|_| CredentialError::InternalError)?
            .map_err(|_| CredentialError::InternalError)?;
        Ok(response)
    }

    /// Simply inserts the |root_hash| into the |get_log| method and
    /// returns the resulting vector of |LogEntry|.
    async fn get_log(&self, root_hash: &Hash) -> Result<Vec<fcr50::LogEntry>, CredentialError> {
        let response = self
            .proxy
            .get_log(&mut root_hash.clone())
            .await
            .map_err(|_| CredentialError::InternalError)?
            .map_err(|_| CredentialError::InternalError)?;
        Ok(response)
    }

    /// Simply inserts the |root_hash|, |h_aux| and |cred_metadata| and
    /// inserts it into the |LogReplayRequest|.
    async fn log_replay(
        &self,
        root_hash: Hash,
        h_aux: Vec<Hash>,
        cred_metadata: CredentialMetadata,
    ) -> Result<fcr50::LogReplayResponse, CredentialError> {
        let log_replay_params = fcr50::LogReplayParams {
            root_hash: Some(root_hash),
            h_aux: Some(h_aux),
            cred_metadata: Some(cred_metadata),
            ..fcr50::LogReplayParams::EMPTY
        };
        let response = self
            .proxy
            .log_replay(log_replay_params)
            .await
            .map_err(|_| CredentialError::InternalError)?
            .map_err(|_| CredentialError::InternalError)?;
        Ok(response)
    }
}

/// Converts the |fcred::DelaySchedule| into a |fcr50::DelaySchedule|.
fn convert_delay_schedule(
    delay_schedule: &Option<Vec<fcred::DelayScheduleEntry>>,
) -> Result<Vec<fcr50::DelayScheduleEntry>, CredentialError> {
    Ok(delay_schedule
        .as_ref()
        .ok_or(CredentialError::InvalidDelaySchedule)?
        .into_iter()
        .map(|e| fcr50::DelayScheduleEntry {
            attempt_count: e.attempt_count,
            time_delay: e.time_delay,
        })
        .collect())
}
