// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{account_manager::AccountId, pinweaver::PinweaverParams, scrypt::ScryptParams},
    async_trait::async_trait,
    fidl_fuchsia_identity_account as faccount, fidl_fuchsia_io as fio, fuchsia_zircon as zx,
    identity_common::StagedFile,
    password_authenticator_config::Config,
    serde::{Deserialize, Serialize},
    std::str::FromStr,
};

#[derive(thiserror::Error, Debug)]
pub enum AccountMetadataStoreError {
    #[error("Failed to open: {0}")]
    OpenError(#[from] fuchsia_fs::node::OpenError),

    #[error("Failed to readdir: {0}")]
    ReaddirError(#[from] fuchsia_fs::directory::Error),

    #[error("Failed during FIDL call: {0}")]
    FidlError(#[from] fidl::Error),

    #[error("Failed to serialize account metadata: {0}")]
    SerdeWriteError(#[source] serde_json::Error),

    #[error("Failed to deserialize account metadata: {0}")]
    SerdeReadError(#[source] serde_json::Error),

    #[error("Failed to write account metadata to backing storage: {0}")]
    WriteError(#[from] fuchsia_fs::file::WriteError),

    #[error("Failed to read account metadata from backing storage: {0}")]
    ReadError(#[from] fuchsia_fs::file::ReadError),

    #[error("Failed to rename account metadata temp file to target: {0}")]
    RenameError(#[from] fuchsia_fs::node::RenameError),

    #[error("Failed to flush account metadata to disk: {0}")]
    FlushError(#[source] zx::Status),

    #[error("Failed to close account metadata backing storage: {0}")]
    CloseError(#[source] zx::Status),

    #[error("Failed to unlink file in backing storage: {0}")]
    UnlinkError(#[source] zx::Status),

    #[error("Failed to operate on staged file: {0}")]
    StagedFileError(#[from] identity_common::StagedFileError),
}

impl From<AccountMetadataStoreError> for faccount::Error {
    fn from(e: AccountMetadataStoreError) -> Self {
        match e {
            AccountMetadataStoreError::OpenError(_) => faccount::Error::Resource,
            AccountMetadataStoreError::ReaddirError(_) => faccount::Error::Resource,
            AccountMetadataStoreError::FidlError(_) => faccount::Error::Resource,
            AccountMetadataStoreError::SerdeWriteError(_) => faccount::Error::Internal,
            AccountMetadataStoreError::SerdeReadError(_) => faccount::Error::Internal,
            AccountMetadataStoreError::WriteError(_) => faccount::Error::Resource,
            AccountMetadataStoreError::ReadError(_) => faccount::Error::Resource,
            AccountMetadataStoreError::RenameError(_) => faccount::Error::Resource,
            AccountMetadataStoreError::FlushError(_) => faccount::Error::Resource,
            AccountMetadataStoreError::CloseError(_) => faccount::Error::Resource,
            AccountMetadataStoreError::UnlinkError(_) => faccount::Error::Resource,
            AccountMetadataStoreError::StagedFileError(_) => faccount::Error::Resource,
        }
    }
}

/// Additional data needed to perform password-only authentication using scrypt
#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
pub struct ScryptOnlyMetadata {
    /// The parameters used with scrypt
    pub scrypt_params: ScryptParams,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct PinweaverMetadata {
    /// The parameters to be used with pinweaver
    pub pinweaver_params: PinweaverParams,
}

/// An enumeration of all supported authenticator metadata types.
#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum AuthenticatorMetadata {
    ScryptOnly(ScryptOnlyMetadata),
    Pinweaver(PinweaverMetadata),
}

// Implement converting each backend's enrollment data into an AuthenticatorMetadata, to be saved
// after enrolling a key.

impl From<ScryptParams> for AuthenticatorMetadata {
    fn from(item: ScryptParams) -> AuthenticatorMetadata {
        AuthenticatorMetadata::ScryptOnly(ScryptOnlyMetadata { scrypt_params: item })
    }
}

impl From<PinweaverParams> for AuthenticatorMetadata {
    fn from(item: PinweaverParams) -> AuthenticatorMetadata {
        AuthenticatorMetadata::Pinweaver(PinweaverMetadata { pinweaver_params: item })
    }
}

// Symmetrically, implement converting each AuthenticatorMetadata variant back into the backend's
// enrollment data, to be used when removing a key.

impl From<ScryptOnlyMetadata> for ScryptParams {
    fn from(item: ScryptOnlyMetadata) -> ScryptParams {
        item.scrypt_params
    }
}

impl From<PinweaverMetadata> for PinweaverParams {
    fn from(item: PinweaverMetadata) -> PinweaverParams {
        item.pinweaver_params
    }
}

/// The data stored at /data/accounts/${ACCOUNT_ID} will have this shape, JSON-serialized.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct AccountMetadata {
    /// The display name associated with this account, provided as `AccountMetadata.name`
    /// at account creation time.
    name: String,

    /// Authenticator-specific metadata
    authenticator_metadata: AuthenticatorMetadata,
}

impl AccountMetadata {
    pub fn new(name: String, authenticator_metadata: AuthenticatorMetadata) -> AccountMetadata {
        AccountMetadata { name, authenticator_metadata }
    }

    /// Returns true iff this type of metadata is allowed by the supplied component configuration.
    pub fn allowed_by_config(&self, config: &Config) -> bool {
        match self.authenticator_metadata {
            AuthenticatorMetadata::ScryptOnly(_) => config.allow_scrypt,
            AuthenticatorMetadata::Pinweaver(_) => config.allow_pinweaver,
        }
    }

    /// Returns the account name.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// Returns the authenticator metadata
    pub fn authenticator_metadata(&self) -> &AuthenticatorMetadata {
        &self.authenticator_metadata
    }
}

/// A trait which abstracts over account metadata I/O.
#[async_trait]
pub trait AccountMetadataStore {
    /// List the ids of all known accounts with metadata stored in this AccountMetadataStore.
    async fn account_ids(&self) -> Result<Vec<AccountId>, AccountMetadataStoreError>;

    /// Serialize and durably record the `metadata` for the specified `account_id`, replacing
    /// any previous data that may have existed.  Returns success if the account was successfully
    /// saved, and error otherwise.
    async fn save(
        &mut self,
        account_id: &AccountId,
        metadata: &AccountMetadata,
    ) -> Result<(), AccountMetadataStoreError>;

    /// Attempt to load durably-stored metadata associated with `account_id`.  Returns an error
    /// if loading the data from storage failed or if the file existed but did not match the
    /// expected format.  Returns success with a None if the account does not exist, and success
    /// with the account metadata if the account was successfully loaded.
    async fn load(
        &self,
        account_id: &AccountId,
    ) -> Result<Option<AccountMetadata>, AccountMetadataStoreError>;

    /// Remove any metadata associated with `account_id`.  Returns success if the account was
    /// successfully removed or if the account did not exist.  Returns an error if the backing
    /// store operation failed.
    async fn remove(&mut self, account_id: &AccountId) -> Result<(), AccountMetadataStoreError>;
}

/// An AccountMetadataStore backed by a filesystem directory handle
pub struct DataDirAccountMetadataStore {
    /// A read-write handle to the /data/accounts directory under which to store and retrieve
    /// serialized account metadata by ID.
    accounts_dir: fio::DirectoryProxy,
}

impl DataDirAccountMetadataStore {
    pub fn new(accounts_dir: fio::DirectoryProxy) -> DataDirAccountMetadataStore {
        DataDirAccountMetadataStore { accounts_dir }
    }

    pub async fn cleanup_stale_files(&mut self) -> Result<(), Vec<AccountMetadataStoreError>> {
        StagedFile::cleanup_stale_files(&self.accounts_dir, "temp-").await.map_err(|errors| {
            errors.into_iter().map(AccountMetadataStoreError::StagedFileError).collect()
        })
    }
}

fn format_account_id(account_id: &AccountId) -> String {
    format!("{}", account_id)
}

fn parse_account_id(text: &str) -> Option<AccountId> {
    let parse_result = u64::from_str(text);
    match parse_result {
        Ok(value) => {
            if format_account_id(&value) == text {
                Some(value)
            } else {
                // Only consider strings which round-trip to themselves.
                None
            }
        }
        Err(_) => None,
    }
}

#[async_trait]
impl AccountMetadataStore for DataDirAccountMetadataStore {
    async fn account_ids(&self) -> Result<Vec<AccountId>, AccountMetadataStoreError> {
        let dirents = fuchsia_fs::directory::readdir(&self.accounts_dir).await?;
        let ids = dirents
            .iter()
            .flat_map(|d| {
                let name = &d.name;
                // Only include files that parse as valid `AccountId`s.
                parse_account_id(name)
            })
            .collect();
        Ok(ids)
    }

    async fn save(
        &mut self,
        account_id: &AccountId,
        metadata: &AccountMetadata,
    ) -> Result<(), AccountMetadataStoreError> {
        let metadata_filename = format_account_id(account_id);

        // Tempfiles will have the prefix "temp-{accountid}"
        let id = format_account_id(account_id);
        let tempfile_prefix = format!("temp-{}", id);

        let mut staged_file = StagedFile::new(&self.accounts_dir, &tempfile_prefix).await?;
        let serialized =
            serde_json::to_vec(&metadata).map_err(AccountMetadataStoreError::SerdeWriteError)?;
        staged_file.write(&serialized).await?;
        staged_file.commit(&metadata_filename).await?;
        Ok(())
    }

    async fn load(
        &self,
        account_id: &AccountId,
    ) -> Result<Option<AccountMetadata>, AccountMetadataStoreError> {
        let metadata_filename = format_account_id(account_id);
        let flags = fio::OpenFlags::RIGHT_READABLE;

        let maybe_file =
            fuchsia_fs::directory::open_file(&self.accounts_dir, &metadata_filename, flags).await;

        let file = match maybe_file {
            Ok(inner) => inner,
            Err(fuchsia_fs::node::OpenError::OpenError(zx::Status::NOT_FOUND)) => return Ok(None),
            Err(err) => return Err(err.into()),
        };

        let serialized = fuchsia_fs::file::read(&file).await?;
        let deserialized = serde_json::from_slice::<AccountMetadata>(&serialized)
            .map_err(AccountMetadataStoreError::SerdeReadError)?;

        Ok(Some(deserialized))
    }

    async fn remove(&mut self, account_id: &AccountId) -> Result<(), AccountMetadataStoreError> {
        let metadata_filename = format_account_id(account_id);
        let res = self.accounts_dir.unlink(&metadata_filename, fio::UnlinkOptions::EMPTY).await?;
        match res {
            Ok(_) => Ok(()),
            Err(err) => Err(AccountMetadataStoreError::UnlinkError(zx::Status::from_raw(err))),
        }
    }
}

#[cfg(test)]
pub mod test {
    use {
        super::*,
        crate::{
            pinweaver::test::TEST_PINWEAVER_CREDENTIAL_LABEL,
            scrypt::test::{FULL_STRENGTH_SCRYPT_PARAMS, TEST_SCRYPT_PARAMS},
        },
        assert_matches::assert_matches,
        lazy_static::lazy_static,
        tempfile::TempDir,
    };

    impl AccountMetadata {
        /// Generate a new AccountMetadata for the scrypt key scheme
        pub fn test_new_scrypt(name: String) -> AccountMetadata {
            let meta = AuthenticatorMetadata::ScryptOnly(ScryptOnlyMetadata {
                scrypt_params: ScryptParams::new(),
            });
            AccountMetadata { name, authenticator_metadata: meta }
        }

        /// Create a new ScryptOnly AccountMetadata using weak scrypt parameters and a fixed salt.
        /// Combined with a known password, this produces a deterministic key for tests.
        pub fn test_new_weak_scrypt(name: String) -> AccountMetadata {
            AccountMetadata {
                name,
                authenticator_metadata: AuthenticatorMetadata::ScryptOnly(ScryptOnlyMetadata {
                    scrypt_params: TEST_SCRYPT_PARAMS,
                }),
            }
        }

        /// Generate a new AccountMetadata for the pinweaver key scheme
        pub fn test_new_pinweaver(name: String) -> AccountMetadata {
            let meta = AuthenticatorMetadata::Pinweaver(PinweaverMetadata {
                pinweaver_params: PinweaverParams {
                    scrypt_params: ScryptParams::new(),
                    credential_label: TEST_PINWEAVER_CREDENTIAL_LABEL,
                },
            });
            AccountMetadata { name, authenticator_metadata: meta }
        }

        /// Create a new Pinweaver AccountMetadata using the same weak scrypt parameters and fixed
        /// salt as test_new_weak_scrypt above.  Combined with a known high-entropy key, this
        /// enables deterministic tests.
        pub fn test_new_weak_pinweaver(name: String) -> AccountMetadata {
            AccountMetadata {
                name,
                authenticator_metadata: AuthenticatorMetadata::Pinweaver(PinweaverMetadata {
                    pinweaver_params: PinweaverParams {
                        scrypt_params: TEST_SCRYPT_PARAMS,
                        credential_label: TEST_PINWEAVER_CREDENTIAL_LABEL,
                    },
                }),
            }
        }
    }

    // This is the user name we include in all test metadata objects
    pub const TEST_NAME: &str = "Test Display Name";

    // These are all valid golden metadata we expect to be able to load.
    const SCRYPT_KEY_AND_NAME_DATA: &[u8] = br#"{"name":"Display Name","authenticator_metadata":{"type":"ScryptOnly","scrypt_params":{"salt":[198,228,57,32,90,251,238,12,194,62,68,106,218,187,24,246],"log_n":15,"r":8,"p":1}}}"#;
    const PINWEAVER_KEY_AND_NAME_DATA: &[u8] = br#"{"name":"Display Name","authenticator_metadata":{"type":"Pinweaver","pinweaver_params":{"scrypt_params":{"salt":[198,228,57,32,90,251,238,12,194,62,68,106,218,187,24,246],"log_n":15,"r":8,"p":1},"credential_label":1}}}"#;

    // This is invalid; it's missing the required authenticator_metadata key.
    // We'll check that we fail to load it.
    const INVALID_METADATA: &[u8] = br#"{"name": "Display Name"}"#;

    lazy_static! {
        pub static ref TEST_SCRYPT_METADATA: AccountMetadata =
            AccountMetadata::test_new_weak_scrypt(TEST_NAME.into());
        pub static ref TEST_PINWEAVER_METADATA: AccountMetadata =
            AccountMetadata::test_new_weak_pinweaver(TEST_NAME.into());
    }

    async fn write_test_file_in_dir(
        dir: &fio::DirectoryProxy,
        path: &std::path::Path,
        data: &[u8],
    ) {
        let file = fuchsia_fs::open_file(
            dir,
            path,
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::CREATE,
        )
        .expect(&format!("create file {}", path.display()));
        file.write(data)
            .await
            .expect(&format!("write file {}", path.display()))
            .map_err(zx::Status::from_raw)
            .expect(&format!("write file {}", path.display()));
        file.close()
            .await
            .expect(&format!("close file {}", path.display()))
            .map_err(zx::Status::from_raw)
            .expect(&format!("close file {}", path.display()));
    }

    #[fuchsia::test]
    fn test_account_id_format() {
        let cases = [
            (0u64, "0"),
            (1u64, "1"),
            (8192u64, "8192"),
            (18446744073709551615u64, "18446744073709551615"),
        ];
        for case in cases.iter() {
            let account_id: AccountId = case.0;
            let expected_str = case.1;
            let formatted = format_account_id(&account_id);
            assert_eq!(formatted, expected_str);
        }
    }

    #[fuchsia::test]
    fn test_account_id_parse() {
        let cases = [
            // Allow all u64 values.
            ("0", Some(0u64)),
            ("1", Some(1u64)),
            ("8192", Some(8192u64)),
            ("18446744073709551615", Some(18446744073709551615u64)),
            // Negative numbers are invalid.
            ("-1", None),
            // Non-canonical expressions of numbers are forbidden.
            ("01", None),
            // Non-base-10 expressions of numbers are forbidden.
            ("0xff", None),
            // Numbers 2^64 or larger are forbidden.
            ("18446744073709551616", None),
        ];
        for case in cases.iter() {
            let s = case.0;
            let expected_parse_result = case.1;
            let parsed = parse_account_id(s);
            assert_eq!(parsed, expected_parse_result);
        }
    }

    #[fuchsia::test]
    fn test_allowed_by_config() {
        const SCRYPT_ONLY_CONFIG: Config = Config { allow_scrypt: true, allow_pinweaver: false };
        const PINWEAVER_ONLY_CONFIG: Config = Config { allow_scrypt: false, allow_pinweaver: true };

        assert!(TEST_SCRYPT_METADATA.allowed_by_config(&SCRYPT_ONLY_CONFIG));
        assert!(!TEST_SCRYPT_METADATA.allowed_by_config(&PINWEAVER_ONLY_CONFIG));
        assert!(!TEST_PINWEAVER_METADATA.allowed_by_config(&SCRYPT_ONLY_CONFIG));
        assert!(TEST_PINWEAVER_METADATA.allowed_by_config(&PINWEAVER_ONLY_CONFIG));
    }

    #[fuchsia::test]
    fn test_metadata_goldens() {
        let deserialized = serde_json::from_slice::<AccountMetadata>(SCRYPT_KEY_AND_NAME_DATA)
            .expect("Deserialize password-only auth metadata");
        assert_eq!(&deserialized.name, "Display Name");
        assert_eq!(
            deserialized.authenticator_metadata,
            AuthenticatorMetadata::ScryptOnly(ScryptOnlyMetadata {
                scrypt_params: FULL_STRENGTH_SCRYPT_PARAMS,
            })
        );
        let reserialized = serde_json::to_vec::<AccountMetadata>(&deserialized)
            .expect("Reserialize password-only auth metadata");
        assert_eq!(reserialized, SCRYPT_KEY_AND_NAME_DATA);

        let deserialized = serde_json::from_slice::<AccountMetadata>(PINWEAVER_KEY_AND_NAME_DATA)
            .expect("Deserialize golden pinweaver auth metadata");
        assert_eq!(&deserialized.name, "Display Name");
        assert_eq!(
            deserialized.authenticator_metadata,
            AuthenticatorMetadata::Pinweaver(PinweaverMetadata {
                pinweaver_params: PinweaverParams {
                    scrypt_params: FULL_STRENGTH_SCRYPT_PARAMS,
                    credential_label: TEST_PINWEAVER_CREDENTIAL_LABEL,
                },
            })
        );
        let reserialized = serde_json::to_vec::<AccountMetadata>(&deserialized)
            .expect("Reserialize pinweaver auth metadata");
        assert_eq!(reserialized, PINWEAVER_KEY_AND_NAME_DATA);
    }

    #[fuchsia::test]
    fn test_metadata_round_trip() {
        let content = AccountMetadata::test_new_scrypt("Display Name".into());
        let serialized = serde_json::to_vec(&content).unwrap();
        let deserialized = serde_json::from_slice::<AccountMetadata>(&serialized).unwrap();
        assert_eq!(content, deserialized);
        assert_eq!(deserialized.name(), "Display Name");

        let content = AccountMetadata::test_new_pinweaver("Display Name".into());
        let serialized = serde_json::to_vec(&content).unwrap();
        let deserialized = serde_json::from_slice::<AccountMetadata>(&serialized).unwrap();
        assert_eq!(content, deserialized);
        assert_eq!(deserialized.name(), "Display Name");
    }

    #[fuchsia::test]
    async fn test_account_metadata_store_save() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = fuchsia_fs::directory::open_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");

        let (dir2, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        dir.clone(
            fio::OpenFlags::empty(),
            fidl::endpoints::ServerEnd::new(server_end.into_channel()),
        )
        .expect("open second connection to temp dir");

        let mut metadata_store = DataDirAccountMetadataStore::new(dir);

        let scrypt_content = AccountMetadata::test_new_scrypt("Display Name".into());
        // Try saving an account to ID 1, and expect it to write new data
        metadata_store.save(&1, &scrypt_content).await.expect("save account 1");

        let dirents = fuchsia_fs::directory::readdir(&dir2).await.expect("readdir");
        assert_eq!(dirents.len(), 1);
        assert_eq!(dirents[0].name, "1");

        // Verify that loading account ID 1 roundtrips the account metadata we just saved.
        let roundtripped = metadata_store.load(&1).await.expect("load account 1");
        assert!(roundtripped.is_some());
        assert_eq!(roundtripped.unwrap(), scrypt_content);

        let pinweaver_content = AccountMetadata::test_new_pinweaver("Display Name".into());

        // Try saving an account to ID 1, and expect it to overwrite the existing data
        metadata_store.save(&1, &pinweaver_content).await.expect("save (overwrite) account 1");

        let dirents = fuchsia_fs::directory::readdir(&dir2).await.expect("readdir");
        assert_eq!(dirents.len(), 1);
        assert_eq!(dirents[0].name, "1");
        let roundtripped = metadata_store.load(&1).await.expect("load account 1, second time");
        assert!(roundtripped.is_some());
        assert_eq!(roundtripped.unwrap(), pinweaver_content);

        metadata_store.save(&2, &pinweaver_content).await.expect("save account 2");
        let dirents = fuchsia_fs::directory::readdir(&dir2).await.expect("readdir");
        assert_eq!(dirents.len(), 2);
        assert_eq!(dirents[0].name, "1");
        assert_eq!(dirents[1].name, "2");
    }

    #[fuchsia::test]
    async fn test_account_metadata_store_load() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = fuchsia_fs::directory::open_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");

        // Prepare tmp_dir with two files, one with valid data and one with invalid data.
        write_test_file_in_dir(&dir, std::path::Path::new("1"), SCRYPT_KEY_AND_NAME_DATA).await;
        write_test_file_in_dir(&dir, std::path::Path::new("2"), INVALID_METADATA).await;

        let metadata_store = DataDirAccountMetadataStore::new(dir);

        // Try loading account ID 1, and expect success
        let load_result = metadata_store.load(&1).await;
        assert_matches!(load_result, Ok(Some(_)));
        let metadata_unwrapped = load_result.unwrap().unwrap();
        assert_eq!(
            metadata_unwrapped,
            AccountMetadata {
                name: "Display Name".into(),
                authenticator_metadata: AuthenticatorMetadata::ScryptOnly(ScryptOnlyMetadata {
                    scrypt_params: FULL_STRENGTH_SCRYPT_PARAMS,
                }),
            }
        );

        // Try loading account ID 2, which has invalid data on disk, and expect no data
        let load_result = metadata_store.load(&2).await;
        assert_matches!(load_result, Err(AccountMetadataStoreError::SerdeReadError(_)));

        // Try loading account ID 3, which does not exist on disk, and expect no data
        let load_result = metadata_store.load(&3).await;
        assert_matches!(load_result, Ok(None));
    }

    #[fuchsia::test]
    async fn test_account_metadata_store_remove() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = fuchsia_fs::directory::open_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");

        let (dir2, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        dir.clone(
            fio::OpenFlags::empty(),
            fidl::endpoints::ServerEnd::new(server_end.into_channel()),
        )
        .expect("open second connection to temp dir");

        // Prepare tmp_dir with an account for ID 1
        write_test_file_in_dir(&dir, std::path::Path::new("1"), SCRYPT_KEY_AND_NAME_DATA).await;

        let mut metadata_store = DataDirAccountMetadataStore::new(dir);

        // First, verify that we can load some metadata
        let load_result = metadata_store.load(&1).await;
        assert!(load_result.is_ok());
        assert!(load_result.unwrap().is_some());

        // Try removing ID 1 and expect success
        let remove_result = metadata_store.remove(&1).await;
        assert!(remove_result.is_ok());

        // Subsequent load attempts should find no account any more, since it's been removed now.
        let load_result = metadata_store.load(&1).await;
        assert!(load_result.is_ok());
        assert!(load_result.unwrap().is_none());

        // And the directory should be empty.
        let dirents = fuchsia_fs::directory::readdir(&dir2).await.expect("readdir");
        assert_eq!(dirents.len(), 0);

        // Try removing ID 2 and expect failure.
        let remove_nonexistent_result = metadata_store.remove(&2).await;
        assert!(remove_nonexistent_result.is_err());
    }

    #[fuchsia::test]
    async fn test_account_metadata_store_account_ids() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = fuchsia_fs::directory::open_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");
        let mut metadata_store = DataDirAccountMetadataStore::new(dir);

        // Store starts empty
        let ids = metadata_store.account_ids().await.expect("account_ids");
        assert_eq!(ids, Vec::<u64>::new());

        // After saving a new account, the store enumerates the account
        let scrypt_meta = AccountMetadata::test_new_scrypt("Display Name".into());
        metadata_store.save(&1, &scrypt_meta).await.expect("save");
        let ids = metadata_store.account_ids().await.expect("account_ids");
        assert_eq!(ids, vec![1u64]);

        // After removing the account, the store does not enumerate the account id
        metadata_store.remove(&1).await.expect("remove");
        let ids = metadata_store.account_ids().await.expect("account_ids");
        assert_eq!(ids, Vec::<u64>::new());
    }

    #[fuchsia::test]
    async fn test_cleanup_stale_files() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = fuchsia_fs::directory::open_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");

        let (dir2, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        dir.clone(
            fio::OpenFlags::empty(),
            fidl::endpoints::ServerEnd::new(server_end.into_channel()),
        )
        .expect("open second connection to temp dir");

        // Prepare tmp_dir with an account for ID 1
        // and a tempfile (representing an uncommitted file), this tempfile
        // matches the "temp-" prefix used when creating and cleaning up
        // |StagedFile|s.
        let temp_filename = "temp-12345-9876";
        write_test_file_in_dir(&dir, std::path::Path::new("1"), SCRYPT_KEY_AND_NAME_DATA).await;
        write_test_file_in_dir(&dir, std::path::Path::new(temp_filename), SCRYPT_KEY_AND_NAME_DATA)
            .await;

        // Expect cleanup to remove the uncommitted file but retain the "1"
        let mut metadata_store = DataDirAccountMetadataStore::new(dir);

        let dirents = fuchsia_fs::directory::readdir(&dir2).await.expect("readdir");
        assert_eq!(dirents.len(), 2);
        assert_eq!(dirents[0].name, "1");
        assert_eq!(dirents[1].name, temp_filename);

        metadata_store.cleanup_stale_files().await.expect("cleanup_stale_files");

        let dirents = fuchsia_fs::directory::readdir(&dir2).await.expect("readdir 2");
        assert_eq!(dirents.len(), 1);
        assert_eq!(dirents[0].name, "1");
    }
}
