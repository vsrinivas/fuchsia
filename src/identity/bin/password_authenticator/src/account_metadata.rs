// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        account_manager::AccountId,
        keys::{Key, KeyDerivation, KeyError},
        options::Options,
        prototype::NullKeyDerivation,
    },
    async_trait::async_trait,
    fidl_fuchsia_identity_account as faccount,
    fidl_fuchsia_io::{DirectoryProxy, UnlinkOptions, OPEN_RIGHT_READABLE},
    fuchsia_zircon as zx,
    identity_common::StagedFile,
    serde::{Deserialize, Serialize},
    std::str::FromStr,
};

#[derive(thiserror::Error, Debug)]
pub enum AccountMetadataStoreError {
    #[error("Failed to open: {0}")]
    OpenError(#[from] io_util::node::OpenError),

    #[error("Failed to readdir: {0}")]
    ReaddirError(#[from] files_async::Error),

    #[error("Failed during FIDL call: {0}")]
    FidlError(#[from] fidl::Error),

    #[error("Failed to serialize account metadata: {0}")]
    SerdeWriteError(#[source] serde_json::Error),

    #[error("Failed to deserialize account metadata: {0}")]
    SerdeReadError(#[source] serde_json::Error),

    #[error("Failed to write account metadata to backing storage: {0}")]
    WriteError(#[from] io_util::file::WriteError),

    #[error("Failed to read account metadata from backing storage: {0}")]
    ReadError(#[from] io_util::file::ReadError),

    #[error("Failed to rename account metadata temp file to target: {0}")]
    RenameError(#[from] io_util::node::RenameError),

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

/// The null key authenticator stores no additional metadata.
#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
pub struct NullAuthMetadata {}

/// Additional data needed to perform password-only authentication using scrypt
#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
pub struct ScryptOnlyMetadata {
    /// The parameters used with scrypt
    scrypt_params: ScryptParams,
}

/// Parameters used with the scrypt key-derivation function.  These match the parameters
/// described in https://datatracker.ietf.org/doc/html/rfc7914
#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
pub struct ScryptParams {
    salt: [u8; 16], // 16 byte random string
    log_n: u8,
    r: u32,
    p: u32,
}

#[async_trait]
impl KeyDerivation for ScryptParams {
    async fn derive_key(&self, password: &str) -> Result<Key, KeyError> {
        let mut output = [0u8; 32];
        let params = scrypt::Params::new(self.log_n, self.r, self.p)
            .map_err(|_| KeyError::KeyDerivationError)?;
        scrypt::scrypt(password.as_bytes(), &self.salt, &params, &mut output)
            .map_err(|_| KeyError::KeyDerivationError)?;
        Ok(output)
    }
}

/// An enumeration of all supported authenticator metadata types.
#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum AuthenticatorMetadata {
    NullKey(NullAuthMetadata),
    ScryptOnly(ScryptOnlyMetadata),
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
    /// Create a new AccountMetadata for the null key scheme
    pub fn new_null(name: String) -> AccountMetadata {
        let meta = AuthenticatorMetadata::NullKey(NullAuthMetadata {});
        AccountMetadata { name, authenticator_metadata: meta }
    }

    /// Generate a new AccountMetadata for the scrypt key scheme, including generating a new salt
    pub fn new_scrypt(name: String) -> AccountMetadata {
        // Generate a new random salt
        let mut salt = [0u8; 16];
        zx::cprng_draw(&mut salt);
        let meta = AuthenticatorMetadata::ScryptOnly(ScryptOnlyMetadata {
            scrypt_params: ScryptParams { salt, log_n: 15, r: 8, p: 1 },
        });
        AccountMetadata { name, authenticator_metadata: meta }
    }

    /// Returns true iff this type of metadata is allowed by the supplied command line options.
    pub fn allowed_by_options(&self, options: &Options) -> bool {
        match self.authenticator_metadata {
            AuthenticatorMetadata::NullKey(_) => options.allow_null,
            AuthenticatorMetadata::ScryptOnly(_) => options.allow_scrypt,
        }
    }

    /// Returns the account name.
    pub fn name(&self) -> &str {
        &self.name
    }
}

#[async_trait]
impl KeyDerivation for AccountMetadata {
    async fn derive_key(&self, password: &str) -> Result<Key, KeyError> {
        match &self.authenticator_metadata {
            AuthenticatorMetadata::NullKey(_) => NullKeyDerivation.derive_key(&password),
            AuthenticatorMetadata::ScryptOnly(s_meta) => s_meta.scrypt_params.derive_key(&password),
        }
        .await
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
    accounts_dir: DirectoryProxy,
}

impl DataDirAccountMetadataStore {
    pub fn new(accounts_dir: DirectoryProxy) -> DataDirAccountMetadataStore {
        DataDirAccountMetadataStore { accounts_dir }
    }

    pub async fn cleanup_stale_files(&mut self) -> Result<(), Vec<AccountMetadataStoreError>> {
        StagedFile::cleanup_stale_files(&self.accounts_dir, "temp-").await.map_err(|errors| {
            errors.into_iter().map(|err| AccountMetadataStoreError::StagedFileError(err)).collect()
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
        let dirents = files_async::readdir(&self.accounts_dir).await?;
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
        let metadata_filename = format_account_id(&account_id);

        // Tempfiles will have the prefix "temp-{accountid}"
        let id = format_account_id(&account_id);
        let tempfile_prefix = format!("temp-{}", id);

        let mut staged_file = StagedFile::new(&self.accounts_dir, &tempfile_prefix).await?;
        let serialized = serde_json::to_vec(&metadata)
            .map_err(|err| AccountMetadataStoreError::SerdeWriteError(err))?;
        staged_file.write(&serialized).await?;
        staged_file.commit(&metadata_filename).await?;
        Ok(())
    }

    async fn load(
        &self,
        account_id: &AccountId,
    ) -> Result<Option<AccountMetadata>, AccountMetadataStoreError> {
        let metadata_filename = format_account_id(&account_id);
        let flags = OPEN_RIGHT_READABLE;

        let maybe_file =
            io_util::directory::open_file(&self.accounts_dir, &metadata_filename, flags).await;

        let file = match maybe_file {
            Ok(inner) => inner,
            Err(io_util::node::OpenError::OpenError(zx::Status::NOT_FOUND)) => return Ok(None),
            Err(err) => return Err(err.into()),
        };

        let serialized = io_util::file::read(&file).await?;
        let deserialized = serde_json::from_slice::<AccountMetadata>(&serialized)
            .map_err(|err| AccountMetadataStoreError::SerdeReadError(err))?;

        Ok(Some(deserialized))
    }

    async fn remove(&mut self, account_id: &AccountId) -> Result<(), AccountMetadataStoreError> {
        let metadata_filename = format_account_id(&account_id);
        let res = self.accounts_dir.unlink(&metadata_filename, UnlinkOptions::EMPTY).await?;
        match res {
            Ok(_) => Ok(()),
            Err(err) => Err(AccountMetadataStoreError::UnlinkError(zx::Status::from_raw(err))),
        }
    }
}

#[cfg(test)]
pub mod test {
    use {
        super::*, assert_matches::assert_matches, fidl_fuchsia_io::DirectoryMarker,
        lazy_static::lazy_static, tempfile::TempDir,
    };

    impl AccountMetadata {
        /// Create a new ScryptOnly AccountMetadata using standard scrypt parameters and the
        /// supplied salt.  Useful for ensuring that (combined with a known password), a
        /// deterministic key can be produced for tests.
        pub fn test_new_weak_scrypt_with_salt(name: String, salt: [u8; 16]) -> AccountMetadata {
            AccountMetadata {
                name,
                authenticator_metadata: AuthenticatorMetadata::ScryptOnly(ScryptOnlyMetadata {
                    scrypt_params: ScryptParams {
                        salt,
                        // We use a very low log_n hardness here to avoid tests burning CPU time
                        // needlessly during unit tests.
                        log_n: 8,
                        r: 8,
                        p: 1,
                    },
                }),
            }
        }
    }

    // This is the user name we include in all test metadata objects
    pub const TEST_NAME: &str = "Test Display Name";

    // These are both valid golden metadata we expect to be able to load.
    const NULL_KEY_AND_NAME_DATA: &[u8] =
        br#"{"name":"Display Name","authenticator_metadata":{"type":"NullKey"}}"#;
    const SCRYPT_KEY_AND_NAME_DATA: &[u8] = br#"{"name":"Display Name","authenticator_metadata":{"type":"ScryptOnly","scrypt_params":{"salt":[138,254,213,33,89,127,189,29,172,66,247,81,102,200,26,205],"log_n":15,"r":8,"p":1}}}"#;

    // This is invalid; it's missing the required authenticator_metadata key.
    // We'll check that we fail to load it.
    const INVALID_METADATA: &[u8] = br#"{"name": "Display Name"}"#;

    // A well-known salt & key for test
    pub const TEST_SCRYPT_SALT: [u8; 16] =
        [202, 26, 165, 102, 212, 113, 114, 60, 106, 121, 183, 133, 36, 166, 127, 146];
    pub const TEST_SCRYPT_PASSWORD: &str = "test password";

    lazy_static! {
        pub static ref TEST_SCRYPT_METADATA: AccountMetadata =
            AccountMetadata::test_new_weak_scrypt_with_salt(TEST_NAME.into(), TEST_SCRYPT_SALT,);
        static ref TEST_NULL_METADATA: AccountMetadata =
            AccountMetadata::new_null(TEST_NAME.into(),);
    }

    // We have precomputed the key produced by the above fixed salt so that each test that wants to
    // use one doesn't need to perform an additional key derivation every single time.  A test
    // ensures that we do the work at least once to make sure our constant is correct.
    pub const TEST_SCRYPT_KEY: [u8; 32] = [
        88, 91, 129, 123, 173, 34, 21, 1, 23, 147, 87, 189, 56, 149, 89, 132, 210, 235, 150, 102,
        129, 93, 202, 53, 115, 170, 162, 217, 254, 115, 216, 181,
    ];

    async fn write_test_file_in_dir(dir: &DirectoryProxy, path: &std::path::Path, data: &[u8]) {
        let file = io_util::open_file(
            &dir,
            path,
            fidl_fuchsia_io::OPEN_RIGHT_READABLE
                | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE
                | fidl_fuchsia_io::OPEN_FLAG_CREATE,
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
            let parsed = parse_account_id(&s);
            assert_eq!(parsed, expected_parse_result);
        }
    }

    #[fuchsia::test]
    fn test_allowed_by_options() {
        const NULL_ONLY_OPTIONS: Options =
            Options { allow_null: true, allow_scrypt: false, allow_pinweaver: false };
        const SCRYPT_ONLY_OPTIONS: Options =
            Options { allow_null: false, allow_scrypt: true, allow_pinweaver: false };

        assert_eq!(TEST_SCRYPT_METADATA.allowed_by_options(&SCRYPT_ONLY_OPTIONS), true);
        assert_eq!(TEST_SCRYPT_METADATA.allowed_by_options(&NULL_ONLY_OPTIONS), false);
        assert_eq!(TEST_NULL_METADATA.allowed_by_options(&NULL_ONLY_OPTIONS), true);
        assert_eq!(TEST_NULL_METADATA.allowed_by_options(&SCRYPT_ONLY_OPTIONS), false);
    }

    #[fuchsia::test]
    fn test_metadata_goldens() {
        let deserialized = serde_json::from_slice::<AccountMetadata>(NULL_KEY_AND_NAME_DATA)
            .expect("Deserialize golden null auth metadata");
        assert_eq!(&deserialized.name, "Display Name");
        assert_matches!(deserialized.authenticator_metadata, AuthenticatorMetadata::NullKey(_));
        let reserialized = serde_json::to_vec::<AccountMetadata>(&deserialized)
            .expect("Reserialize null auth metadata");
        assert_eq!(reserialized, NULL_KEY_AND_NAME_DATA);

        let deserialized = serde_json::from_slice::<AccountMetadata>(SCRYPT_KEY_AND_NAME_DATA)
            .expect("Deserialize password-only auth metadata");
        assert_eq!(&deserialized.name, "Display Name");
        assert_matches!(
            deserialized.authenticator_metadata,
            AuthenticatorMetadata::ScryptOnly(ScryptOnlyMetadata {
                scrypt_params: ScryptParams {
                    salt: [
                        138u8, 254u8, 213u8, 33u8, 89u8, 127u8, 189u8, 29u8, 172u8, 66u8, 247u8,
                        81u8, 102u8, 200u8, 26u8, 205u8
                    ],
                    log_n: 15,
                    r: 8,
                    p: 1,
                },
            })
        );
        let reserialized = serde_json::to_vec::<AccountMetadata>(&deserialized)
            .expect("Reserialize password-only auth metadata");
        assert_eq!(reserialized, SCRYPT_KEY_AND_NAME_DATA);
    }

    #[fuchsia::test]
    async fn test_scrypt_key_derivation_weak_for_tests() {
        let key = TEST_SCRYPT_METADATA.derive_key(TEST_SCRYPT_PASSWORD).await.expect("derive_key");
        assert_eq!(key, TEST_SCRYPT_KEY);
    }

    #[fuchsia::test]
    async fn test_scrypt_key_derivation_full_strength() {
        // Tests the full-strength key derivation against separately-verified constants.
        const GOLDEN_SCRYPT_SALT: [u8; 16] =
            [198, 228, 57, 32, 90, 251, 238, 12, 194, 62, 68, 106, 218, 187, 24, 246];

        const GOLDEN_SCRYPT_PASSWORD: &str = "test password";
        const GOLDEN_SCRYPT_KEY: [u8; 32] = [
            27, 250, 228, 96, 145, 67, 194, 114, 144, 240, 92, 150, 43, 136, 128, 51, 223, 120, 56,
            118, 124, 122, 106, 185, 159, 111, 178, 50, 86, 243, 227, 175,
        ];

        let meta = AccountMetadata {
            name: "Test Display Name".into(),
            authenticator_metadata: AuthenticatorMetadata::ScryptOnly(ScryptOnlyMetadata {
                scrypt_params: ScryptParams { salt: GOLDEN_SCRYPT_SALT, log_n: 15, r: 8, p: 1 },
            }),
        };
        let key = meta.derive_key(GOLDEN_SCRYPT_PASSWORD).await.expect("derive_key");
        assert_eq!(key, GOLDEN_SCRYPT_KEY);
    }

    #[fuchsia::test]
    fn test_metadata_round_trip() {
        let content = AccountMetadata::new_null("Display Name".into());
        let serialized = serde_json::to_vec(&content).unwrap();
        let deserialized = serde_json::from_slice::<AccountMetadata>(&serialized).unwrap();
        assert_eq!(content, deserialized);
        assert_eq!(deserialized.name(), "Display Name");

        let content = AccountMetadata::new_scrypt("Display Name".into());
        let serialized = serde_json::to_vec(&content).unwrap();
        let deserialized = serde_json::from_slice::<AccountMetadata>(&serialized).unwrap();
        assert_eq!(content, deserialized);
        assert_eq!(deserialized.name(), "Display Name");
    }

    #[fuchsia::test]
    async fn test_account_metadata_store_save() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = io_util::open_directory_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");

        let (dir2, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        dir.clone(0, fidl::endpoints::ServerEnd::new(server_end.into_channel()))
            .expect("open second connection to temp dir");

        let mut metadata_store = DataDirAccountMetadataStore::new(dir);

        let null_content = AccountMetadata::new_null("Display Name".into());
        // Try saving an account to ID 1, and expect it to write new data
        metadata_store.save(&1, &null_content).await.expect("save account 1");

        let dirents = files_async::readdir(&dir2).await.expect("readdir");
        assert_eq!(dirents.len(), 1);
        assert_eq!(dirents[0].name, "1");

        // Verify that loading account ID 1 roundtrips the account metadata we just saved.
        let roundtripped = metadata_store.load(&1).await.expect("load account 1");
        assert!(roundtripped.is_some());
        assert_eq!(roundtripped.unwrap(), null_content);

        let scrypt_content = AccountMetadata::new_scrypt("Display Name".into());

        // Try saving an account to ID 1, and expect it to overwrite the existing data
        metadata_store.save(&1, &scrypt_content).await.expect("save (overwrite) account 1");

        let dirents = files_async::readdir(&dir2).await.expect("readdir");
        assert_eq!(dirents.len(), 1);
        assert_eq!(dirents[0].name, "1");
        let roundtripped = metadata_store.load(&1).await.expect("load account 1, second time");
        assert!(roundtripped.is_some());
        assert_eq!(roundtripped.unwrap(), scrypt_content);

        metadata_store.save(&2, &scrypt_content).await.expect("save account 2");
        let dirents = files_async::readdir(&dir2).await.expect("readdir");
        assert_eq!(dirents.len(), 2);
        assert_eq!(dirents[0].name, "1");
        assert_eq!(dirents[1].name, "2");
    }

    #[fuchsia::test]
    async fn test_account_metadata_store_load() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = io_util::open_directory_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
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
                    scrypt_params: ScryptParams {
                        salt: [
                            138u8, 254u8, 213u8, 33u8, 89u8, 127u8, 189u8, 29u8, 172u8, 66u8,
                            247u8, 81u8, 102u8, 200u8, 26u8, 205u8
                        ],
                        log_n: 15,
                        r: 8,
                        p: 1,
                    },
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
        let dir = io_util::open_directory_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");

        let (dir2, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        dir.clone(0, fidl::endpoints::ServerEnd::new(server_end.into_channel()))
            .expect("open second connection to temp dir");

        // Prepare tmp_dir with an account for ID 1
        write_test_file_in_dir(&dir, std::path::Path::new("1"), NULL_KEY_AND_NAME_DATA).await;

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
        let dirents = files_async::readdir(&dir2).await.expect("readdir");
        assert_eq!(dirents.len(), 0);

        // Try removing ID 2 and expect failure.
        let remove_nonexistent_result = metadata_store.remove(&2).await;
        assert!(remove_nonexistent_result.is_err());
    }

    #[fuchsia::test]
    async fn test_account_metadata_store_account_ids() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = io_util::open_directory_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");
        let mut metadata_store = DataDirAccountMetadataStore::new(dir);

        // Store starts empty
        let ids = metadata_store.account_ids().await.expect("account_ids");
        assert_eq!(ids, Vec::<u64>::new());

        // After saving a new account, the store enumerates the account
        let scrypt_meta = AccountMetadata::new_scrypt("Display Name".into());
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
        let dir = io_util::open_directory_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");

        let (dir2, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        dir.clone(0, fidl::endpoints::ServerEnd::new(server_end.into_channel()))
            .expect("open second connection to temp dir");

        // Prepare tmp_dir with an account for ID 1
        // and a tempfile (representing an uncommitted file), this tempfile
        // matches the "temp-" prefix used when creating and cleaning up
        // |StagedFile|s.
        let temp_filename = "temp-12345-9876";
        write_test_file_in_dir(&dir, std::path::Path::new("1"), NULL_KEY_AND_NAME_DATA).await;
        write_test_file_in_dir(&dir, std::path::Path::new(temp_filename), NULL_KEY_AND_NAME_DATA)
            .await;

        // Expect cleanup to remove the uncommitted file but retain the "1"
        let mut metadata_store = DataDirAccountMetadataStore::new(dir);

        let dirents = files_async::readdir(&dir2).await.expect("readdir");
        assert_eq!(dirents.len(), 2);
        assert_eq!(dirents[0].name, "1");
        assert_eq!(dirents[1].name, temp_filename);

        metadata_store.cleanup_stale_files().await.expect("cleanup_stale_files");

        let dirents = files_async::readdir(&dir2).await.expect("readdir 2");
        assert_eq!(dirents.len(), 1);
        assert_eq!(dirents[0].name, "1");
    }
}
