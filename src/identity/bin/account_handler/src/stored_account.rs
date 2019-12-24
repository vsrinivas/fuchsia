// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use account_common::{AccountManagerError, GlobalAccountId, LocalPersonaId};
use fidl_fuchsia_identity_account::Error as ApiError;
use log::warn;
use serde_derive::{Deserialize, Serialize};
use std::fs::{self, File};
use std::io::{BufReader, BufWriter, Write};
use std::path::{Path, PathBuf};

/// Name of account doc file (one per account), within the account's dir.
const ACCOUNT_DOC: &str = "account.json";

/// Name of temporary account doc file, within the account's dir.
const ACCOUNT_DOC_TMP: &str = "account.json.tmp";

/// Json-representation of Fuchsia account state, on disk. As this format evolves,
/// cautiousness is encouraged to ensure backwards compatibility.
#[derive(Serialize, Deserialize)]
pub struct StoredAccount {
    /// Default persona id for this account
    default_persona_id: LocalPersonaId,
    /// Global id for this account.
    global_account_id: GlobalAccountId,
}

/// TODO(dnordstrom): Improve this API to better match the user intent rather than to expose paths
/// as-is, making the user check for existence, removing the actual file, etc.
impl StoredAccount {
    /// Create a new stored account. No side effects.
    pub fn new(
        default_persona_id: LocalPersonaId,
        global_account_id: GlobalAccountId,
    ) -> StoredAccount {
        Self { default_persona_id, global_account_id }
    }

    /// Get the default persona id.
    pub fn get_default_persona_id(&self) -> &LocalPersonaId {
        &self.default_persona_id
    }

    /// Get the global account id.
    pub fn get_global_account_id(&self) -> &GlobalAccountId {
        &self.global_account_id
    }

    /// Load StoredAccount from disk
    pub fn load(account_dir: &Path) -> Result<StoredAccount, AccountManagerError> {
        let path = Self::path(account_dir);
        if !path.exists() {
            warn!("Failed to locate account doc: {:?}", path);
            return Err(AccountManagerError::new(ApiError::NotFound));
        };
        let file = BufReader::new(File::open(path).map_err(|err| {
            warn!("Failed to read account doc: {:?}", err);
            AccountManagerError::new(ApiError::Resource).with_cause(err)
        })?);
        serde_json::from_reader(file).map_err(|err| {
            warn!("Failed to parse account doc: {:?}", err);
            AccountManagerError::new(ApiError::Internal).with_cause(err)
        })
    }

    /// Get the path to the account doc, given the account_dir.
    pub fn path(account_dir: &Path) -> PathBuf {
        account_dir.join(ACCOUNT_DOC)
    }

    /// Convenience path to the doc temp file, given the account_dir; used for safe writing
    fn tmp_path(account_dir: &Path) -> PathBuf {
        account_dir.join(ACCOUNT_DOC_TMP)
    }

    /// Write StoredAccount to disk, ensuring the file is either written completely or not
    /// modified.
    pub fn save(&self, account_dir: &Path) -> Result<(), AccountManagerError> {
        let path = Self::path(account_dir);
        let tmp_path = Self::tmp_path(account_dir);
        {
            let mut tmp_file = BufWriter::new(File::create(&tmp_path).map_err(|err| {
                warn!("Failed to create account tmp doc: {:?}", err);
                AccountManagerError::new(ApiError::Resource).with_cause(err)
            })?);
            serde_json::to_writer(&mut tmp_file, self).map_err(|err| {
                warn!("Failed to serialize account doc: {:?}", err);
                AccountManagerError::new(ApiError::Resource).with_cause(err)
            })?;
            tmp_file.flush().map_err(|err| {
                warn!("Failed to flush serialized account doc: {:?}", err);
                AccountManagerError::new(ApiError::Resource).with_cause(err)
            })?;
        }
        fs::rename(&tmp_path, &path).map_err(|err| {
            warn!("Failed to rename account doc: {:?}", err);
            AccountManagerError::new(ApiError::Resource).with_cause(err)
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[test]
    fn load_error() {
        let tmp_dir = TempDir::new().unwrap();
        let err = StoredAccount::load(&tmp_dir.path()).err().expect("load unexpectedly succeeded");
        assert_eq!(err.api_error, ApiError::NotFound);
    }

    #[test]
    fn invalid_json() {
        let tmp_dir = TempDir::new().unwrap();
        let data = "<INVALID_JSON>";
        fs::write(StoredAccount::path(&tmp_dir.path()), data).expect("failed writing test data");
        let err = StoredAccount::load(&tmp_dir.path()).err().expect("load unexpectedly succeeded");
        assert_eq!(err.api_error, ApiError::Internal);
    }

    #[test]
    fn save_and_load() {
        let tmp_dir = TempDir::new().unwrap();
        let stored =
            StoredAccount::new(LocalPersonaId::new(6), GlobalAccountId::new(vec![1, 2, 3]));
        assert!(stored.save(&tmp_dir.path()).is_ok());
        let stored = StoredAccount::load(&tmp_dir.path()).expect("failed loading stored account");
        assert_eq!(stored.get_default_persona_id(), &LocalPersonaId::new(6));
        assert_eq!(stored.get_global_account_id(), &GlobalAccountId::new(vec![1, 2, 3]));
        assert!(stored.save(&tmp_dir.path()).is_ok()); // Checking that save works a second time
    }

    #[test]
    fn save_non_existing() {
        let tmp_dir = TempDir::new().unwrap();
        let path = tmp_dir.path().join("santa");
        let stored = StoredAccount::new(LocalPersonaId::new(4), GlobalAccountId::new(vec![0, 2]));
        let err = stored.save(&path).err().expect("save unexpectedly succeeded");
        assert_eq!(err.api_error, ApiError::Resource);
    }
}
