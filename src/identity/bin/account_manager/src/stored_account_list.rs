// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


use log::warn;
use std::fs::{self, File};
use std::io::{BufReader, BufWriter, Write};
use std::path::{Path, PathBuf};

use account_common::{AccountManagerError, LocalAccountId};
use fidl_fuchsia_identity_account::Error as ApiError;
use serde_derive::{Deserialize, Serialize};

/// Name of account list file (one per account manager), within the account list dir.
const ACCOUNT_LIST_DOC: &str = "list.json";

/// Name of temporary account list file, within the account list dir.
const ACCOUNT_LIST_DOC_TMP: &str = "list.json.tmp";

#[derive(Clone, Serialize, Deserialize)]
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

    /// Load StoredAccountList from disk. If `account_list_dir` does not exist, it will be created.
    /// If it cannot be created, an error will be returned. The rationale for creating the dir in
    /// `load` as opposed to in `save` is to fail early, since load is generally called before save.
    pub fn load(account_list_dir: &Path) -> Result<StoredAccountList, AccountManagerError> {
        if !account_list_dir.exists() {
            fs::create_dir_all(account_list_dir).map_err(|err| {
                warn!("Failed to create account list dir: {:?}", account_list_dir);
                AccountManagerError::new(ApiError::Resource).with_cause(err)
            })?;
            warn!("Created account list dir: {:?}", account_list_dir);
            return Ok(StoredAccountList::new(vec![]));
        };
        let path = Self::path(account_list_dir);
        if !path.exists() {
            warn!("Account list not found, initializing empty: {:?}", path);
            return Ok(StoredAccountList::new(vec![]));
        };
        let file = BufReader::new(File::open(path).map_err(|err| {
            warn!("Failed to read account list: {:?}", err);
            AccountManagerError::new(ApiError::Resource).with_cause(err)
        })?);
        serde_json::from_reader(file).map_err(|err| {
            warn!("Failed to parse account list: {:?}", err);
            AccountManagerError::new(ApiError::Internal).with_cause(err)
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
            let mut tmp_file = BufWriter::new(File::create(&tmp_path).map_err(|err| {
                warn!("Failed to create account tmp list: {:?}", err);
                AccountManagerError::new(ApiError::Resource).with_cause(err)
            })?);
            serde_json::to_writer(&mut tmp_file, self).map_err(|err| {
                warn!("Failed to serialize account list: {:?}", err);
                AccountManagerError::new(ApiError::Resource).with_cause(err)
            })?;
            tmp_file.flush().map_err(|err| {
                warn!("Failed to flush serialized account list: {:?}", err);
                AccountManagerError::new(ApiError::Resource).with_cause(err)
            })?;
        }
        fs::rename(&tmp_path, &path).map_err(|err| {
            warn!("Failed to rename account list: {:?}", err);
            AccountManagerError::new(ApiError::Resource).with_cause(err)
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use lazy_static::lazy_static;
    use tempfile::TempDir;

    lazy_static! {
        static ref ACCOUNT_ID_1: LocalAccountId = LocalAccountId::new(13);
        static ref ACCOUNT_ID_2: LocalAccountId = LocalAccountId::new(1);
        static ref ACCOUNT_META_1: StoredAccountMetadata =
            StoredAccountMetadata::new(ACCOUNT_ID_1.clone());
        static ref ACCOUNT_META_2: StoredAccountMetadata =
            StoredAccountMetadata::new(ACCOUNT_ID_2.clone());
    }

    #[test]
    fn load_invalid_dir() {
        let err = StoredAccountList::load(Path::new("/invalid"))
            .err()
            .expect("load unexpectedly succeeded");
        assert_eq!(err.api_error, ApiError::Resource);
    }

    #[test]
    fn load_from_non_existing_dir_then_save() -> Result<(), AccountManagerError> {
        let tmp_dir = TempDir::new().unwrap();
        let sub_dir = PathBuf::from(tmp_dir.path()).join("sub").join("dir");
        let loaded = StoredAccountList::load(&sub_dir)?;
        assert_eq!(loaded.accounts().len(), 0);
        loaded.save(&sub_dir)
    }

    #[test]
    fn load_invalid_json() {
        let tmp_dir = TempDir::new().unwrap();
        let data = "<INVALID_JSON>";
        fs::write(StoredAccountList::path(&tmp_dir.path()), data)
            .expect("failed writing test data");
        let err =
            StoredAccountList::load(&tmp_dir.path()).err().expect("load unexpectedly succeeded");
        assert_eq!(err.api_error, ApiError::Internal);
    }

    #[test]
    fn save_then_load_then_save() -> Result<(), AccountManagerError> {
        let tmp_dir = TempDir::new().unwrap();
        let to_save = StoredAccountList::new(vec![ACCOUNT_META_1.clone(), ACCOUNT_META_2.clone()]);
        to_save.save(&tmp_dir.path())?;
        let loaded = StoredAccountList::load(&tmp_dir.path())?;
        let accounts = loaded.accounts();
        assert_eq!(accounts[0].account_id(), &*ACCOUNT_ID_1);
        assert_eq!(accounts[1].account_id(), &*ACCOUNT_ID_2);
        loaded.save(&tmp_dir.path())
    }
}
