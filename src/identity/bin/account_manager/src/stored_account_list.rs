// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    account_common::{AccountId, AccountManagerError},
    anyhow::format_err,
    fidl_fuchsia_identity_account::{AccountMetadata as FidlAccountMetadata, Error as ApiError},
    serde::{Deserialize, Serialize},
    std::{
        collections::{btree_map::Values as BTreeMapValues, BTreeMap},
        fs::{self, File},
        io::{BufReader, BufWriter, Write},
        path::{Path, PathBuf},
    },
    tracing::warn,
};

/// Name of account list file (one per account manager), within the account list dir.
const ACCOUNT_LIST_DOC: &str = "list.json";

/// Name of temporary account list file, within the account list dir.
const ACCOUNT_LIST_DOC_TMP: &str = "list.json.tmp";

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct StoredAccount {
    /// Account id for this account
    account_id: AccountId,

    /// PreAuthState for the account
    pre_auth_state: Vec<u8>,

    /// Some basic information associated with the account
    metadata: AccountMetadata,
}

impl StoredAccount {
    pub fn new(
        account_id: AccountId,
        pre_auth_state: Vec<u8>,
        metadata: AccountMetadata,
    ) -> StoredAccount {
        Self { account_id, pre_auth_state, metadata }
    }

    pub fn account_id(&self) -> &AccountId {
        &self.account_id
    }

    pub fn set_pre_auth_state(&mut self, pre_auth_state: Vec<u8>) {
        self.pre_auth_state = pre_auth_state
    }

    pub fn pre_auth_state(&self) -> &Vec<u8> {
        &self.pre_auth_state
    }

    pub fn metadata(&self) -> &AccountMetadata {
        &self.metadata
    }
}

/// Basic data about a system account.
///
/// This is generated from the equivalent struct in the FIDL bindings and
/// used to add serialization attributes.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct AccountMetadata {
    /// A human-readable name for the account
    name: String,
}

impl AccountMetadata {
    pub fn new(name: String) -> Self {
        Self { name }
    }
}

impl TryFrom<FidlAccountMetadata> for AccountMetadata {
    type Error = ApiError;
    fn try_from(mut account_metadata: FidlAccountMetadata) -> Result<Self, Self::Error> {
        let name = account_metadata.name.take().ok_or_else(|| {
            warn!("Unable to convert AccountMetadata without name");
            ApiError::InvalidRequest
        })?;

        Ok(Self::new(name))
    }
}

impl<'a> Into<FidlAccountMetadata> for &'a AccountMetadata {
    fn into(self) -> FidlAccountMetadata {
        FidlAccountMetadata { name: Some(self.name.to_string()), ..FidlAccountMetadata::EMPTY }
    }
}

/// Json-representation of the set of system accounts on device. As this format evolves,
/// cautiousness is encouraged to ensure backwards compatibility.
#[derive(Serialize, Deserialize)]
#[serde(transparent)]
pub struct StoredAccountList {
    /// The directory where the account list resides.
    #[serde(skip)]
    account_list_dir: PathBuf,

    /// A map representing the PreAuthState for each account.
    accounts: BTreeMap<AccountId, StoredAccount>,
}

impl StoredAccountList {
    /// Get the Pre Auth State for the specified account_id.
    pub fn get_account_pre_auth_state(
        &self,
        account_id: &AccountId,
    ) -> Result<&Vec<u8>, AccountManagerError> {
        match self.accounts.get(account_id) {
            None => {
                let cause = format_err!("ID {:?} not found", &account_id);
                Err(AccountManagerError::new(ApiError::NotFound).with_cause(cause))
            }
            Some(stored_account) => Ok(stored_account.pre_auth_state()),
        }
    }

    /// Get the metadata for the specified account_id.
    pub fn get_metadata(
        &self,
        account_id: &AccountId,
    ) -> Result<&AccountMetadata, AccountManagerError> {
        match self.accounts.get(account_id) {
            None => {
                let cause = format_err!("ID {:?} not found", &account_id);
                Err(AccountManagerError::new(ApiError::NotFound).with_cause(cause))
            }
            Some(stored_account) => Ok(stored_account.metadata()),
        }
    }

    /// Create a new stored account list from the given accounts.
    /// Uses `account_list_dir` to persist the account list.
    pub fn new(account_list_dir: &Path, accounts: Vec<StoredAccount>) -> StoredAccountList {
        let mut account_map = BTreeMap::new();
        for account in accounts {
            account_map.insert(account.account_id().clone(), account);
        }
        Self { account_list_dir: account_list_dir.to_path_buf(), accounts: account_map }
    }

    /// Add a new account to the list and save the updated list
    /// to the disk.
    pub fn add_account(
        &mut self,
        account_id: &AccountId,
        pre_auth_state: Vec<u8>,
        metadata: AccountMetadata,
    ) -> Result<(), AccountManagerError> {
        if self.accounts.contains_key(account_id) {
            let cause = format_err!("Duplicate ID {:?} creating new account", &account_id);
            return Err(AccountManagerError::new(ApiError::Internal).with_cause(cause));
        }

        self.accounts.insert(
            account_id.clone(),
            StoredAccount::new(account_id.clone(), pre_auth_state, metadata),
        );

        self.save()
    }

    /// Update the pre_auth_state for an account that already exists in the list
    /// and save the updated list to the disk.
    pub fn update_account(
        &mut self,
        account_id: &AccountId,
        pre_auth_state: Vec<u8>,
    ) -> Result<(), AccountManagerError> {
        match self.accounts.get_mut(account_id) {
            None => {
                let cause = format_err!("ID {:?} not found", &account_id);
                Err(AccountManagerError::new(ApiError::Internal).with_cause(cause))
            }
            Some(stored_account) => {
                stored_account.set_pre_auth_state(pre_auth_state);
                self.save()
            }
        }
    }

    /// Remove an existing account from the list and save the updated list
    /// to the disk.
    pub fn remove_account(&mut self, account_id: &AccountId) -> Result<(), AccountManagerError> {
        match self.accounts.get(account_id) {
            None => {
                let cause = format_err!("ID {:?} not found", &account_id);
                Err(AccountManagerError::new(ApiError::Internal).with_cause(cause))
            }
            Some(_) => {
                self.accounts.remove(account_id);
                self.save()
            }
        }
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
            return Ok(StoredAccountList::new(account_list_dir, vec![]));
        };
        let path = Self::path(account_list_dir);
        if !path.exists() {
            warn!(?path, "Account list not found, initializing empty");
            return Ok(StoredAccountList::new(account_list_dir, vec![]));
        };
        let file = BufReader::new(File::open(path).map_err(|err| {
            warn!("Failed to read account list: {:?}", err);
            AccountManagerError::new(ApiError::Resource).with_cause(err)
        })?);
        let mut stored_account_list: StoredAccountList =
            serde_json::from_reader(file).map_err(|err| {
                warn!("Failed to parse account list: {:?}", err);
                AccountManagerError::new(ApiError::Internal).with_cause(err)
            })?;

        // Explicitly populate the non-serialized fields in StoredAccountList.
        stored_account_list.account_list_dir = PathBuf::from(account_list_dir);

        Ok(stored_account_list)
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
    pub fn save(&self) -> Result<(), AccountManagerError> {
        let path = Self::path(self.account_list_dir.as_path());
        let tmp_path = Self::tmp_path(self.account_list_dir.as_path());
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

impl<'a> IntoIterator for &'a StoredAccountList {
    type Item = &'a StoredAccount;
    type IntoIter = BTreeMapValues<'a, AccountId, StoredAccount>;

    fn into_iter(self) -> Self::IntoIter {
        self.accounts.values()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use lazy_static::lazy_static;
    use tempfile::TempDir;

    lazy_static! {
        static ref ACCOUNT_ID_1: AccountId = AccountId::new(13);
        static ref ACCOUNT_ID_2: AccountId = AccountId::new(1);
        static ref ACCOUNT_PRE_AUTH_STATE_1: Vec<u8> = vec![1, 2, 3];
        static ref ACCOUNT_PRE_AUTH_STATE_2: Vec<u8> = vec![2, 4, 6];
        static ref ACCOUNT_METADATA_1: AccountMetadata = AccountMetadata::new("test1".to_string());
        static ref ACCOUNT_METADATA_2: AccountMetadata = AccountMetadata::new("test2".to_string());
        static ref STORED_ACCOUNT_1: StoredAccount = StoredAccount::new(
            ACCOUNT_ID_1.clone(),
            ACCOUNT_PRE_AUTH_STATE_1.to_vec(),
            ACCOUNT_METADATA_1.clone(),
        );
        static ref STORED_ACCOUNT_2: StoredAccount = StoredAccount::new(
            ACCOUNT_ID_2.clone(),
            ACCOUNT_PRE_AUTH_STATE_2.to_vec(),
            ACCOUNT_METADATA_2.clone(),
        );
        static ref STORED_ACCOUNTS_SERIALIZED_STR: String =
            "{\"1\":{\"account_id\":1,\"pre_auth_state\":[2,4,6],\
            \"metadata\":{\"name\":\"test2\"}},\
            \"13\":{\"account_id\":13,\"pre_auth_state\":[1,2,3],\
            \"metadata\":{\"name\":\"test1\"}}}"
                .to_string();
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
        assert_eq!(loaded.into_iter().count(), 0);
        loaded.save()
    }

    #[test]
    fn load_invalid_json() {
        let tmp_dir = TempDir::new().unwrap();
        let data = "<INVALID_JSON>";
        fs::write(StoredAccountList::path(tmp_dir.path()), data).expect("failed writing test data");
        let err =
            StoredAccountList::load(&tmp_dir.path()).err().expect("load unexpectedly succeeded");
        assert_eq!(err.api_error, ApiError::Internal);
    }

    #[test]
    fn save_then_load_then_save() -> Result<(), AccountManagerError> {
        let tmp_dir = TempDir::new().unwrap();
        let to_save = StoredAccountList::new(
            &tmp_dir.path(),
            vec![STORED_ACCOUNT_1.clone(), STORED_ACCOUNT_2.clone()],
        );
        to_save.save()?;
        let loaded = StoredAccountList::load(&tmp_dir.path())?;
        let mut accounts = loaded.into_iter();
        let account1 = accounts.next().unwrap();
        let account2 = accounts.next().unwrap();
        assert_eq!(account1.account_id(), &*ACCOUNT_ID_2);
        assert_eq!(account1.pre_auth_state(), &*ACCOUNT_PRE_AUTH_STATE_2);
        assert_eq!(account1.metadata(), &*ACCOUNT_METADATA_2);
        assert_eq!(account2.account_id(), &*ACCOUNT_ID_1);
        assert_eq!(account2.pre_auth_state(), &*ACCOUNT_PRE_AUTH_STATE_1);
        assert_eq!(account2.metadata(), &*ACCOUNT_METADATA_1);

        loaded.save()
    }

    #[test]
    fn add_update_remove() -> Result<(), AccountManagerError> {
        let tmp_dir = TempDir::new().unwrap();
        let mut stored_account_list = StoredAccountList::new(
            &tmp_dir.path(),
            vec![STORED_ACCOUNT_1.clone(), STORED_ACCOUNT_2.clone()],
        );
        {
            let mut accounts = stored_account_list.into_iter();
            let account1 = accounts.next().unwrap();
            let account2 = accounts.next().unwrap();
            assert_eq!(account1.account_id(), &*ACCOUNT_ID_2);
            assert_eq!(account1.pre_auth_state(), &*ACCOUNT_PRE_AUTH_STATE_2);
            assert_eq!(account1.metadata(), &*ACCOUNT_METADATA_2);
            assert_eq!(account2.account_id(), &*ACCOUNT_ID_1);
            assert_eq!(account2.pre_auth_state(), &*ACCOUNT_PRE_AUTH_STATE_1);
            assert_eq!(account2.metadata(), &*ACCOUNT_METADATA_1);

            assert_eq!(None, accounts.next());
        }

        stored_account_list.remove_account(&*ACCOUNT_ID_2)?;
        {
            let mut accounts = stored_account_list.into_iter();
            let account1 = accounts.next().unwrap();
            assert_eq!(account1.account_id(), &*ACCOUNT_ID_1);
            assert_eq!(account1.pre_auth_state(), &*ACCOUNT_PRE_AUTH_STATE_1);
            assert_eq!(account1.metadata(), &*ACCOUNT_METADATA_1);

            assert_eq!(None, accounts.next());
        }

        stored_account_list.update_account(&*ACCOUNT_ID_1, ACCOUNT_PRE_AUTH_STATE_2.to_vec())?;
        {
            let mut accounts = stored_account_list.into_iter();
            let account1 = accounts.next().unwrap();
            assert_eq!(account1.account_id(), &*ACCOUNT_ID_1);
            assert_eq!(account1.pre_auth_state(), &*ACCOUNT_PRE_AUTH_STATE_2);

            assert_eq!(None, accounts.next());
        }

        // Cannot update non-existent account.
        assert_eq!(
            stored_account_list
                .update_account(&*ACCOUNT_ID_2, ACCOUNT_PRE_AUTH_STATE_2.to_vec())
                .unwrap_err()
                .api_error,
            ApiError::Internal
        );

        // Cannot remove non-existent account.
        assert_eq!(
            stored_account_list.remove_account(&*ACCOUNT_ID_2).unwrap_err().api_error,
            ApiError::Internal
        );

        Ok(())
    }

    #[test]
    fn serialize_to_string() {
        let tmp_dir = TempDir::new().unwrap();
        let account_list = StoredAccountList::new(
            &tmp_dir.path(),
            vec![STORED_ACCOUNT_1.clone(), STORED_ACCOUNT_2.clone()],
        );
        assert_eq!(serde_json::to_string(&account_list).unwrap(), *STORED_ACCOUNTS_SERIALIZED_STR);
    }
}
