// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::label::Label,
    async_trait::async_trait,
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
    identity_common::StagedFile,
    std::fmt,
    thiserror::Error,
    tracing::{info, warn},
};

#[cfg(test)]
use mockall::{automock, predicate::*};

/// The directory where the lookup table data is stored.
pub const LOOKUP_TABLE_PATH: &str = "/data/creds";
const STAGEDFILE_PREFIX: &str = "temp-";

// Contents in a LookupTable are versioned.
type Version = u64;

// Read results consist of the contents and the latest version.
#[derive(Debug)]
pub struct ReadResult {
    pub bytes: Vec<u8>,
    pub version: Version,
}

#[derive(Error, Debug)]
pub struct ResetTableError(Vec<LookupTableError>);

impl From<Vec<LookupTableError>> for ResetTableError {
    fn from(error: Vec<LookupTableError>) -> Self {
        ResetTableError(error)
    }
}

impl fmt::Display for ResetTableError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", self.0)
    }
}

#[derive(Error, Debug)]
pub enum LookupTableError {
    #[error("Failed to open: {0}")]
    OpenError(#[from] fuchsia_fs::node::OpenError),
    #[error("Failed to readdir: {0}")]
    ReaddirError(#[from] fuchsia_fs::directory::Error),
    #[error("Failed to read: {0}")]
    ReadError(#[from] fuchsia_fs::file::ReadError),
    #[error("Failed during FIDL call: {0}")]
    FidlError(#[from] fidl::Error),
    #[error("Failed finding latest version")]
    NotFound,
    #[error("Failed to unlink file in backing storage: {0}")]
    UnlinkError(#[source] zx::Status),
    #[error("Failed to operate on staged file: {0}")]
    StagedFileError(#[from] identity_common::StagedFileError),
    #[error("Reset Failed")]
    ResetTableError(#[from] ResetTableError),
    #[error("Unknown lookup table error")]
    Unknown,
}

#[cfg_attr(test, automock)]
#[async_trait]
pub trait LookupTable {
    /// Writes |data| to a versioned entry for |label|.
    async fn write(&mut self, label: &Label, data: Vec<u8>) -> Result<(), LookupTableError>;
    /// Reads the latest versioned entry for |label| and returns the result.
    async fn read(&self, label: &Label) -> Result<ReadResult, LookupTableError>;
    /// Deletes all versioned entries for |label|.
    /// Future writes for the same label will begin at the initial version.
    /// Deleting a |label that has no associated values will return a NotFound
    /// error.
    async fn delete(&mut self, label: &Label) -> Result<(), LookupTableError>;
    /// Resets the entire lookup table deleting all of the credentials. This
    /// is used if the hash_tree enters an unrecoverable state usually
    /// due to a power outage.
    async fn reset(&mut self) -> Result<(), LookupTableError>;
}

/// Implements |LookupTable| with a persistent directory backing it.
/// Multiple versions of each label are stored to support incremental log
/// replay. A subdirectory is created for each label and contains a file for
/// each version of the label.
/// Upon initialization, the same directory should be provided in order to
/// allow for persistence across initialization.
pub struct PersistentLookupTable {
    dir_proxy: fio::DirectoryProxy,
}

impl PersistentLookupTable {
    pub fn new(dir_proxy: fio::DirectoryProxy) -> PersistentLookupTable {
        Self { dir_proxy }
    }

    /// Cleanup stale files in the directory. Ideally, this should be called
    /// before operating on the |label|.
    /// If the label does not have a directory, this proceeds optimistically.
    /// TODO(arkay): Determine the contract for cleaning up stale files.
    #[allow(dead_code)]
    async fn cleanup_stale_files(&mut self, label: &Label) -> Result<(), Vec<LookupTableError>> {
        // Try to open the label's subdirectory, if it exists.
        match fuchsia_fs::directory::open_directory(
            &self.dir_proxy,
            &label.as_dir_name(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        {
            Ok(child_dir) => {
                StagedFile::cleanup_stale_files(&child_dir, STAGEDFILE_PREFIX).await.map_err(
                    |errors| errors.into_iter().map(LookupTableError::StagedFileError).collect(),
                )
            }
            Err(err) => {
                info!(?label, %err, "Could not open label subdirectory for cleanup");
                Ok(())
            }
        }
    }

    /// Retrieve the latest version number of files in a directory.
    async fn get_latest_version(
        &self,
        dir: &fio::DirectoryProxy,
    ) -> Result<Option<Version>, LookupTableError> {
        // Look through the directory for the latest version number.
        let dirents = fuchsia_fs::directory::readdir(dir).await?;
        let mut latest_version = None;
        // Since fuchsia_fs::directory::readdir returns files in a deterministic order,
        // it is possible to avoid looping through all files via padding
        // filenames. But we are continuing to use the loop in order to gain
        // data around if/how often unexpected filenames are found, and to
        // lower the overhead of padding u64 filenames, with the understanding
        // that we should clean up old versions.
        // TODO(arkay): Clean up old versions on startup after ensuring we
        // have caught up to CR50.
        for entry in dirents {
            match parse_version(&entry.name) {
                Some(version) => match latest_version {
                    Some(curr_version) => {
                        if curr_version < version {
                            latest_version = Some(version);
                        }
                    }
                    None => {
                        latest_version = Some(version);
                    }
                },
                None => {
                    // Skip any entry name we can't parse.
                    // TODO(arkay): Should we error out when we see something we don't expect?
                    warn!("Found unexpected entry name: {}", entry.name);
                    continue;
                }
            }
        }

        Ok(latest_version)
    }
}

fn format_version(val: &Version) -> String {
    // Since we do not do any padding on the version, the expected format of
    // a version is it's base10 represented value.
    format!("{}", val)
}

fn parse_version(val: &str) -> Option<Version> {
    let parse_result = val.parse::<u64>();
    match parse_result {
        Ok(version) => {
            if format_version(&version) == val {
                Some(version)
            } else {
                None
            }
        }
        Err(_) => None,
    }
}

#[async_trait]
impl LookupTable for PersistentLookupTable {
    async fn write(&mut self, label: &Label, data: Vec<u8>) -> Result<(), LookupTableError> {
        // Create the directory if it doesn't already exist.
        let child_dir = fuchsia_fs::directory::create_directory(
            &self.dir_proxy,
            &label.as_dir_name(),
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::CREATE,
        )
        .await?;

        // Start version numbering at 1.
        // TODO(arkay): Ensure this works with CR50Agent's expectations.
        let next_version = match self.get_latest_version(&child_dir).await? {
            Some(version) => version + 1,
            None => 1,
        };

        let mut staged_file = StagedFile::new(&child_dir, STAGEDFILE_PREFIX).await?;
        staged_file.write(&data).await?;
        staged_file.commit(&format_version(&next_version)).await?;
        Ok(())
    }

    async fn read(&self, label: &Label) -> Result<ReadResult, LookupTableError> {
        // Try to open the label's subdirectory.
        let child_dir = fuchsia_fs::directory::open_directory(
            &self.dir_proxy,
            &label.as_dir_name(),
            fio::OpenFlags::RIGHT_READABLE,
        )
        .await
        .map_err(|_| LookupTableError::NotFound)?;

        // Find the latest version for the label.
        let latest_version = match self.get_latest_version(&child_dir).await? {
            Some(version) => version,
            None => {
                warn!("did not find a latest version for label directory {:?}", label);
                return Err(LookupTableError::NotFound);
            }
        };

        let latest_file = fuchsia_fs::directory::open_file(
            &child_dir,
            &format_version(&latest_version),
            fio::OpenFlags::RIGHT_READABLE,
        )
        .await?;
        let file_bytes = fuchsia_fs::file::read(&latest_file).await?;
        Ok(ReadResult { bytes: file_bytes, version: latest_version })
    }

    async fn delete(&mut self, label: &Label) -> Result<(), LookupTableError> {
        // Since we don't want to rely on remove_dir_recursive returning a
        // Fidl error with a string identifier, we manually try to open the
        // directory to determine if it exists.
        fuchsia_fs::directory::open_directory(
            &self.dir_proxy,
            &label.as_dir_name(),
            fio::OpenFlags::RIGHT_READABLE,
        )
        .await
        .map_err(|_| LookupTableError::NotFound)?;

        fuchsia_fs::directory::remove_dir_recursive(&self.dir_proxy, &label.as_dir_name())
            .await
            .map_err(|e| match e {
                fuchsia_fs::directory::Error::Fidl(_, fidl_err) => {
                    LookupTableError::FidlError(fidl_err)
                }
                fuchsia_fs::directory::Error::Unlink(status) => {
                    LookupTableError::UnlinkError(status)
                }
                _ => LookupTableError::Unknown,
            })
    }

    async fn reset(&mut self) -> Result<(), LookupTableError> {
        let dir_result = fuchsia_fs::directory::readdir(&self.dir_proxy).await;
        let dir_entries = dir_result.map_err(|err| {
            LookupTableError::ResetTableError(vec![LookupTableError::ReaddirError(err)].into())
        })?;
        let mut failures = Vec::new();
        for entry in dir_entries.iter() {
            if let Err(e) =
                fuchsia_fs::directory::remove_dir_recursive(&self.dir_proxy, &entry.name)
                    .await
                    .map_err(|e| match e {
                        fuchsia_fs::directory::Error::Fidl(_, fidl_err) => {
                            LookupTableError::FidlError(fidl_err)
                        }
                        fuchsia_fs::directory::Error::Unlink(status) => {
                            LookupTableError::UnlinkError(status)
                        }
                        _ => LookupTableError::Unknown,
                    })
            {
                failures.push(e);
            }
        }
        if failures.is_empty() {
            Ok(())
        } else {
            Err(LookupTableError::ResetTableError(failures.into()))
        }
    }
}

#[cfg(test)]
mod test {
    use {super::*, crate::label::TEST_LABEL, assert_matches::assert_matches, tempfile::TempDir};

    #[fuchsia::test]
    async fn test_read_before_write() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = fuchsia_fs::directory::open_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");
        let plt = PersistentLookupTable::new(dir);

        assert!(plt.read(&TEST_LABEL).await.is_err());
    }

    #[fuchsia::test]
    async fn test_read_fails_if_label_directory_is_empty() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = fuchsia_fs::directory::open_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");
        let mut plt = PersistentLookupTable::new(dir);

        plt.write(&TEST_LABEL, b"foo bar".to_vec()).await.unwrap();
        let read_res = plt.read(&TEST_LABEL).await.unwrap();

        assert_eq!(read_res.bytes, b"foo bar".to_vec());
        assert_eq!(read_res.version, 1);

        // Manually delete files in the label directory.
        let dir_2 = fuchsia_fs::directory::open_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");
        let child_dir = fuchsia_fs::directory::create_directory(
            &dir_2,
            &TEST_LABEL.as_dir_name(),
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::CREATE,
        )
        .await
        .unwrap();
        for entry in fuchsia_fs::directory::readdir(&child_dir).await.unwrap() {
            child_dir.unlink(&entry.name, fio::UnlinkOptions::EMPTY).await.unwrap().unwrap();
        }

        // Ensure that now we have an error since the label directory is empty.
        assert!(plt.read(&TEST_LABEL).await.is_err());
    }

    #[fuchsia::test]
    async fn test_multi_versioned_writes() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = fuchsia_fs::directory::open_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");
        let mut plt = PersistentLookupTable::new(dir);

        plt.write(&TEST_LABEL, b"foo bar".to_vec()).await.unwrap();
        let mut read_res = plt.read(&TEST_LABEL).await.unwrap();

        assert_eq!(read_res.bytes, b"foo bar".to_vec());
        assert_eq!(read_res.version, 1);

        plt.write(&TEST_LABEL, b"foo bar 2".to_vec()).await.unwrap();
        read_res = plt.read(&TEST_LABEL).await.unwrap();

        assert_eq!(read_res.bytes, b"foo bar 2".to_vec());
        assert_eq!(read_res.version, 2);
    }

    #[fuchsia::test]
    async fn test_write_delete() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = fuchsia_fs::directory::open_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");
        let mut plt = PersistentLookupTable::new(dir);

        plt.write(&TEST_LABEL, b"foo bar".to_vec()).await.unwrap();
        let read_res = plt.read(&TEST_LABEL).await.unwrap();

        assert_eq!(read_res.bytes, b"foo bar".to_vec());
        assert_eq!(read_res.version, 1);

        plt.delete(&TEST_LABEL).await.unwrap();
        assert!(plt.read(&TEST_LABEL).await.is_err());
    }

    #[fuchsia::test]
    async fn test_delete_before_write() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = fuchsia_fs::directory::open_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");
        let mut plt = PersistentLookupTable::new(dir);

        let res = plt.delete(&TEST_LABEL).await;
        assert!(res.is_err());
        match res.unwrap_err() {
            LookupTableError::NotFound => {
                // We got the correct error
            }
            err => {
                panic!("expected NotFound error {}", err)
            }
        }
    }

    #[fuchsia::test]
    async fn test_ignores_bad_dir_entry() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = fuchsia_fs::directory::open_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");

        // Write a bad entry to the expected directory.
        let child_dir = fuchsia_fs::directory::create_directory(
            &dir,
            &TEST_LABEL.as_dir_name(),
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::CREATE,
        )
        .await
        .unwrap();
        let bad_file = fuchsia_fs::directory::open_file(
            &child_dir,
            "bad file name",
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::CREATE,
        )
        .await
        .unwrap();
        bad_file.write(b"foo bar 2".as_ref()).await.unwrap().map_err(zx::Status::from_raw).unwrap();
        bad_file.close().await.unwrap().map_err(zx::Status::from_raw).unwrap();

        let mut plt = PersistentLookupTable::new(dir);

        // Ensure that despite the bad file, we can still write and read values.
        plt.write(&TEST_LABEL, b"foo bar".to_vec()).await.unwrap();
        let read_res = plt.read(&TEST_LABEL).await.unwrap();

        assert_eq!(read_res.bytes, b"foo bar".to_vec());
        assert_eq!(read_res.version, 1);
    }

    #[fuchsia::test]
    fn test_parse_version() {
        assert_eq!(parse_version("5"), Some(5));
        assert_eq!(parse_version("18"), Some(18));
        assert_eq!(parse_version("40018"), Some(40018));
        assert_eq!(parse_version("65535"), Some(65535));

        // Expected failed parsing follows.
        // Greater than u64::MAX
        assert_eq!(parse_version("18446744073709551616"), None);
        // Version names should not have any padding as per format_version
        assert_eq!(parse_version("0005"), None);
        // Decimals
        assert_eq!(parse_version("5.0"), None);
        // Negative
        assert_eq!(parse_version("-17"), None);
        // Non base-10
        assert_eq!(parse_version("0xff"), None);
    }

    #[fuchsia::test]
    async fn test_stale_files_cleaned_up() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = fuchsia_fs::directory::open_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");

        // Write a staged file to the label's directory.
        let child_dir = fuchsia_fs::directory::create_directory(
            &dir,
            &TEST_LABEL.as_dir_name(),
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::CREATE,
        )
        .await
        .unwrap();
        let stale_file = fuchsia_fs::directory::open_file(
            &child_dir,
            &format!("{}01234", STAGEDFILE_PREFIX),
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::CREATE,
        )
        .await
        .unwrap();
        stale_file
            .write(b"stale_file_content".as_ref())
            .await
            .unwrap()
            .map_err(zx::Status::from_raw)
            .unwrap();
        stale_file.close().await.unwrap().map_err(zx::Status::from_raw).unwrap();

        // Inititalize and cleanup stale files for TEST_LABEL.
        let mut plt = PersistentLookupTable::new(dir);
        plt.cleanup_stale_files(&TEST_LABEL).await.unwrap();

        // Check that after initializing the lookup table, we have cleaned up
        // stale files.
        let entries = fuchsia_fs::directory::readdir(&child_dir).await.unwrap();
        assert!(entries.is_empty());

        // Check that we can write and read from the directory as expected.
        plt.write(&TEST_LABEL, b"foo bar".to_vec()).await.unwrap();
        let content = plt.read(&TEST_LABEL).await.unwrap();
        assert_eq!(content.bytes, b"foo bar".to_vec());
    }

    #[fuchsia::test]
    async fn test_reset() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = fuchsia_fs::directory::open_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");
        let mut plt = PersistentLookupTable::new(dir);
        plt.write(&TEST_LABEL, b"foo bar".to_vec()).await.unwrap();
        plt.read(&TEST_LABEL).await.unwrap();
        plt.reset().await.unwrap();
        assert_matches!(plt.read(&TEST_LABEL).await, Err(LookupTableError::NotFound));
    }
}
