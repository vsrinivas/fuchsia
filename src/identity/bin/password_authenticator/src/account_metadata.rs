// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::account_manager::AccountId,
    async_trait::async_trait,
    fidl_fuchsia_io::{
        DirectoryProxy, OPEN_FLAG_CREATE, OPEN_FLAG_TRUNCATE, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_io2::UnlinkOptions,
    fuchsia_zircon as zx,
    log::warn,
    rand::{thread_rng, Rng},
    serde::{Deserialize, Serialize},
    std::str::FromStr,
};

// Some things will be unused until future patchsets make use of them.
#[allow(dead_code)]
#[allow(unused)]
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

/// An enumeration of all supported authenticator metadata types.
#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
#[serde(tag = "type")]
enum AuthenticatorMetadata {
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
    #[allow(dead_code)]
    pub fn new_null(name: String) -> AccountMetadata {
        let meta = AuthenticatorMetadata::NullKey(NullAuthMetadata {});
        AccountMetadata { name, authenticator_metadata: meta }
    }

    /// Generate a new AccountMetadata for the scrypt key scheme, including generating a new salt
    #[allow(dead_code)]
    pub fn new_scrypt(name: String) -> AccountMetadata {
        // Generate a new random salt
        let mut salt = [0u8; 16];
        zx::cprng_draw(&mut salt);
        let meta = AuthenticatorMetadata::ScryptOnly(ScryptOnlyMetadata {
            scrypt_params: ScryptParams { salt, log_n: 15, r: 8, p: 1 },
        });
        AccountMetadata { name, authenticator_metadata: meta }
    }
}

/// A trait which abstracts over account metadata I/O.
#[async_trait]
pub trait AccountMetadataStore {
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
        let dirents_res = files_async::readdir(&self.accounts_dir).await;
        let dirents =
            dirents_res.map_err(|err| vec![AccountMetadataStoreError::ReaddirError(err.into())])?;
        let mut failures = Vec::new();

        for d in dirents.iter() {
            let name = &d.name;
            // We aim to delete files that don't look like an AccountId (which is a u64),
            // as well as files that are not the canonical representation of that number.
            if parse_account_id(&name).is_none() {
                // Not a valid account id.  Try to remove it.
                warn!("Removing unexpected file '{}' from account metadata directory", &name);
                let fidl_res = self.accounts_dir.unlink(&name, UnlinkOptions::EMPTY).await;
                match fidl_res {
                    Err(x) => failures.push(AccountMetadataStoreError::FidlError(x.into())),
                    Ok(unlink_res) => {
                        if let Err(unlink_err) = unlink_res {
                            failures.push(AccountMetadataStoreError::UnlinkError(
                                zx::Status::from_raw(unlink_err),
                            ));
                        }
                    }
                }
            }
        }

        if failures.len() == 0 {
            Ok(())
        } else {
            Err(failures)
        }
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

// I'd rather have used the tempfile crate here, but it lacks any ability to open things
// without making use of paths and namespaces, and we're specifically trying to use a
// directory handle
const TEMPFILE_RANDOM_LENGTH: usize = 8usize;
fn generate_tempfile_name(account_id: &AccountId) -> String {
    // Generate a tempfile with name "temp-{accountid}-{random}"
    let id = format_account_id(&account_id);
    let mut buf = String::with_capacity(TEMPFILE_RANDOM_LENGTH + id.len() + 6);
    buf.push_str("temp-");
    buf.push_str(&id);
    buf.push('-');
    let mut rng = thread_rng();
    std::iter::repeat(())
        .map(|()| rng.sample(rand::distributions::Alphanumeric))
        .map(char::from)
        .take(TEMPFILE_RANDOM_LENGTH)
        .for_each(|c| buf.push(c));
    return buf;
}

#[async_trait]
impl AccountMetadataStore for DataDirAccountMetadataStore {
    async fn save(
        &mut self,
        account_id: &AccountId,
        metadata: &AccountMetadata,
    ) -> Result<(), AccountMetadataStoreError> {
        let metadata_filename = format_account_id(&account_id);
        let flags =
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE | OPEN_FLAG_TRUNCATE;
        let temp_filename = generate_tempfile_name(&account_id);
        let file = io_util::directory::open_file(&self.accounts_dir, &temp_filename, flags).await?;
        let serialized = serde_json::to_vec(&metadata)
            .map_err(|err| AccountMetadataStoreError::SerdeWriteError(err))?;

        // Do the usual atomic commit via tempfile open, write, sync, close, and rename-to-target.
        // Stale files left by a crash will be cleaned up by calling cleanup_stale_files on the
        // next startup.
        io_util::file::write(&file, &serialized).await?;
        zx::Status::ok(file.sync().await?).map_err(|s| AccountMetadataStoreError::FlushError(s))?;
        zx::Status::ok(file.close().await?)
            .map_err(|s| AccountMetadataStoreError::CloseError(s))?;

        // Commit
        io_util::directory::rename(&self.accounts_dir, &temp_filename, &metadata_filename)
            .await
            .map_err(|s| AccountMetadataStoreError::RenameError(s))?;

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
mod test {
    use {super::*, fidl_fuchsia_io::DirectoryMarker, matches::assert_matches, tempfile::TempDir};

    // These are both valid golden metadata we expect to be able to load.
    const NULL_KEY_AND_NAME_DATA: &[u8] =
        br#"{"name":"Display Name","authenticator_metadata":{"type":"NullKey"}}"#;
    const SCRYPT_KEY_AND_NAME_DATA: &[u8] = br#"{"name":"Display Name","authenticator_metadata":{"type":"ScryptOnly","scrypt_params":{"salt":[138,254,213,33,89,127,189,29,172,66,247,81,102,200,26,205],"log_n":15,"r":8,"p":1}}}"#;

    // This is invalid; it's missing the required authenticator_metadata key.
    // We'll check that we fail to load it.
    const INVALID_METADATA: &[u8] = br#"{"name": "Display Name"}"#;

    async fn write_test_file_in_dir(dir: &DirectoryProxy, path: &std::path::Path, data: &[u8]) {
        let file = io_util::open_file(
            &dir,
            path,
            fidl_fuchsia_io::OPEN_RIGHT_READABLE
                | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE
                | fidl_fuchsia_io::OPEN_FLAG_CREATE,
        )
        .expect(&format!("create file {}", path.display()));
        file.write(data).await.expect(&format!("write file {}", path.display()));
        file.close().await.expect(&format!("close file {}", path.display()));
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
    fn test_generate_tempfile_name() {
        let name1 = generate_tempfile_name(&1);
        let name2 = generate_tempfile_name(&1);
        let prefix = "temp-1-";
        assert!(name1.starts_with(prefix));
        assert!(name2.starts_with(prefix));
        assert_eq!(name1.len(), prefix.len() + TEMPFILE_RANDOM_LENGTH);
        assert_eq!(name2.len(), prefix.len() + TEMPFILE_RANDOM_LENGTH);
        assert_ne!(name1, name2);
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
    fn test_metadata_round_trip() {
        let content = AccountMetadata::new_null("Display Name".into());
        let serialized = serde_json::to_vec(&content).unwrap();
        let deserialized = serde_json::from_slice::<AccountMetadata>(&serialized).unwrap();
        assert_eq!(content, deserialized);

        let content = AccountMetadata::new_scrypt("Display Name".into());
        let serialized = serde_json::to_vec(&content).unwrap();
        let deserialized = serde_json::from_slice::<AccountMetadata>(&serialized).unwrap();
        assert_eq!(content, deserialized);
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
        // and a tempfile (representing an uncommitted file)
        let temp_filename = ".Yv8VrVzrCk";
        write_test_file_in_dir(&dir, std::path::Path::new("1"), NULL_KEY_AND_NAME_DATA).await;
        write_test_file_in_dir(&dir, std::path::Path::new(temp_filename), NULL_KEY_AND_NAME_DATA)
            .await;

        // Expect cleanup to remove the uncommitted file but retain the "1"
        let mut metadata_store = DataDirAccountMetadataStore::new(dir);

        let dirents = files_async::readdir(&dir2).await.expect("readdir");
        assert_eq!(dirents.len(), 2);
        assert_eq!(dirents[0].name, temp_filename);
        assert_eq!(dirents[1].name, "1");

        metadata_store.cleanup_stale_files().await.expect("cleanup_stale_files");

        let dirents = files_async::readdir(&dir2).await.expect("readdir 2");
        assert_eq!(dirents.len(), 1);
        assert_eq!(dirents[0].name, "1");
    }
}
