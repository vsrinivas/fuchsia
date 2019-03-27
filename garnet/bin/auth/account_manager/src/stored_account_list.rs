// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate serde;
extern crate serde_json;

use log::warn;
use std::fs::{self, File};
use std::path::{Path, PathBuf};

use account_common::{AccountManagerError, LocalAccountId};
use fidl_fuchsia_auth_account::Status;
use serde_derive::{Deserialize, Serialize};

/// Name of account list file (one per account manager), within the account list dir.
const ACCOUNT_LIST_DOC: &str = "list.json";

/// Name of temporary account list file, within the account list dir.
const ACCOUNT_LIST_DOC_TMP: &str = "list.json.tmp";

#[derive(Serialize, Deserialize)]
pub struct StoredAccountMetadata {
    /// Local account id for this account
    account_id: LocalAccountId,
}

impl StoredAccountMetadata {
    pub fn new(account_id: LocalAccountId) -> StoredAccountMetadata {
        Self { account_id }
    }

    pub fn account_id(&self) -> &LocalAccountId {
        &self.account_id
    }
}

/// Json-representation of the set of Fuchsia accounts on device. As this format evolves,
/// cautiousness is encouraged to ensure backwards compatibility.
#[derive(Serialize, Deserialize)]
#[serde(transparent)]
pub struct StoredAccountList {
    accounts: Vec<StoredAccountMetadata>,
}

impl StoredAccountList {
    /// Create a new stored account. No side effects.
    pub fn new(accounts: Vec<StoredAccountMetadata>) -> StoredAccountList {
        Self { accounts }
    }

    /// List of each account's metadata which are part of this list.
    pub fn accounts(&self) -> &Vec<StoredAccountMetadata> {
        &self.accounts
    }

    /// Load StoredAccountList from disk
    pub fn load(account_list_dir: &Path) -> Result<StoredAccountList, AccountManagerError> {
        let path = Self::path(account_list_dir);
        if !path.exists() {
            warn!("Created account list dir: {:?}", path);
            return Ok(StoredAccountList::new(vec![]));
        };
        let file = File::open(path).map_err(|err| {
            warn!("Failed to read account list: {:?}", err);
            AccountManagerError::new(Status::IoError).with_cause(err)
        })?;
        serde_json::from_reader(file).map_err(|err| {
            warn!("Failed to parse account list: {:?}", err);
            AccountManagerError::new(Status::InternalError).with_cause(err)
        })
    }

    /// Convenience path to the list file, given the account_list_dir
    fn path(account_list_dir: &Path) -> PathBuf {
        account_list_dir.join(ACCOUNT_LIST_DOC)
    }

    /// Convenience path to the list temp file, given the account_list_dir; used for safe writing
    fn tmp_path(account_list_dir: &Path) -> PathBuf {
        account_list_dir.join(ACCOUNT_LIST_DOC_TMP)
    }

    /// Write StoredAccountList to disk, ensuring the file is either written completely or not
    /// modified.
    pub fn save(&self, account_list_dir: &Path) -> Result<(), AccountManagerError> {
        let path = Self::path(account_list_dir);
        let tmp_path = Self::tmp_path(account_list_dir);
        {
            let tmp_file = File::create(&tmp_path).map_err(|err| {
                warn!("Failed to create account tmp list: {:?}", err);
                AccountManagerError::new(Status::IoError).with_cause(err)
            })?;
            serde_json::to_writer(tmp_file, self).map_err(|err| {
                warn!("Failed to write account list: {:?}", err);
                AccountManagerError::new(Status::IoError).with_cause(err)
            })?;
        }
        fs::rename(&tmp_path, &path).map_err(|err| {
            warn!("Failed to rename account list: {:?}", err);
            AccountManagerError::new(Status::IoError).with_cause(err)
        })
    }
}
