// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![allow(dead_code)]

use {
    crate::label_generator::Label,
    async_trait::async_trait,
    fidl_fuchsia_io::{DirectoryProxy, OPEN_FLAG_CREATE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fuchsia_zircon as zx,
    identity_common::StagedFile,
    log::{info, warn},
    thiserror::Error,
};

const STAGEDFILE_PREFIX: &str = "temp-";

// Contents in a LookupTable are versioned.
type Version = u64;

// Read results consist of the contents and the latest version.
#[derive(Debug)]
struct ReadResult {
    bytes: Vec<u8>,
    version: Version,
}

#[derive(Error, Debug)]
pub enum LookupTableError {
    #[error("Failed to open: {0}")]
    OpenError(#[from] io_util::node::OpenError),
    #[error("Failed to readdir: {0}")]
    ReaddirError(#[from] files_async::Error),
    #[error("Failed to read: {0}")]
    ReadError(#[from] io_util::file::ReadError),
    #[error("Failed during FIDL call: {0}")]
    FidlError(#[from] fidl::Error),
    #[error("Failed finding latest version")]
    NotFound,
    #[error("Failed to unlink file in backing storage: {0}")]
    UnlinkError(#[source] zx::Status),
    #[error("Failed to operate on staged file: {0}")]
    StagedFileError(#[from] identity_common::StagedFileError),
    #[error("Unknown lookup table error")]
    Unknown,
}

#[async_trait]
trait LookupTable {
    /// Writes |data| to a versioned entry for |label|.
    async fn write(&mut self, label: &Label, data: Vec<u8>) -> Result<(), LookupTableError>;
    /// Reads the latest versioned entry for |label| and returns the result.
    async fn read(&self, label: &Label) -> Result<ReadResult, LookupTableError>;
    /// Deletes all versioned entries for |label|.
    /// Future writes for the same label will begin at the initial version.
    /// Deleting a |label that has no associated values will return a NotFound
    /// error.
    async fn delete(&mut self, label: &Label) -> Result<(), LookupTableError>;
}

/// Implements |LookupTable| with a persistent directory backing it.
/// Multiple versions of each label are stored to support incremental log
/// replay. A subdirectory is created for each label and contains a file for
/// each version of the label.
/// Upon initialization, the same directory should be provided in order to
/// allow for persistence across initialization.
struct PersistentLookupTable {
    dir_proxy: DirectoryProxy,
}

impl PersistentLookupTable {
    pub fn new(dir_proxy: DirectoryProxy) -> PersistentLookupTable {
        Self { dir_proxy }
    }

    /// Cleanup stale files in the directory. Ideally, this should be called
    /// before operating on the |label|.
    /// If the label does not have a directory, this proceeds optimistically.
    /// TODO(arkay): Determine the contract for cleaning up stale files.
    async fn cleanup_stale_files(&mut self, label: &Label) -> Result<(), Vec<LookupTableError>> {
        // Try to open the label's subdirectory, if it exists.
        match io_util::directory::open_directory(
            &self.dir_proxy,
            &label.into_dir_name(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .await
        {
            Ok(child_dir) => StagedFile::cleanup_stale_files(&child_dir, STAGEDFILE_PREFIX)
                .await
                .map_err(|errors| {
                    errors.into_iter().map(|err| LookupTableError::StagedFileError(err)).collect()
                }),
            Err(err) => {
                info!("Could not open subdirectory for label {:?} for cleanup: {}", label, err);
                Ok(())
            }
        }
    }

    /// Retrieve the latest version number of files in a directory.
    async fn get_latest_version(
        &self,
        dir: &DirectoryProxy,
    ) -> Result<Option<Version>, LookupTableError> {
        // Look through the directory for the latest version number.
        let dirents = files_async::readdir(dir).await?;
        let mut latest_version = None;
        // Since files_async::readdir returns files in a deterministic order,
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
        let child_dir = io_util::directory::create_directory(
            &self.dir_proxy,
            &label.into_dir_name(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE,
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
        let child_dir = io_util::directory::open_directory(
            &self.dir_proxy,
            &label.into_dir_name(),
            OPEN_RIGHT_READABLE,
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

        let latest_file = io_util::directory::open_file(
            &child_dir,
            &format_version(&latest_version),
            OPEN_RIGHT_READABLE,
        )
        .await?;
        let file_bytes = io_util::file::read(&latest_file).await?;
        Ok(ReadResult { bytes: file_bytes, version: latest_version })
    }

    async fn delete(&mut self, label: &Label) -> Result<(), LookupTableError> {
        // Since we don't want to rely on remove_dir_recursive returning a
        // Fidl error with a string identifier, we manually try to open the
        // directory to determine if it exists.
        io_util::directory::open_directory(
            &self.dir_proxy,
            &label.into_dir_name(),
            OPEN_RIGHT_READABLE,
        )
        .await
        .map_err(|_| LookupTableError::NotFound)?;

        files_async::remove_dir_recursive(&self.dir_proxy, &label.into_dir_name()).await.map_err(
            |e| match e {
                files_async::Error::Fidl(_, fidl_err) => LookupTableError::FidlError(fidl_err),
                files_async::Error::Unlink(status) => LookupTableError::UnlinkError(status),
                _ => LookupTableError::Unknown,
            },
        )
    }
}

#[cfg(test)]
mod test {
    use {
        super::*, crate::label_generator::TEST_LABEL, fidl_fuchsia_io::UnlinkOptions,
        tempfile::TempDir,
    };

    #[fuchsia::test]
    async fn test_read_before_write() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = io_util::open_directory_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");
        let plt = PersistentLookupTable::new(dir);

        assert!(plt.read(&TEST_LABEL).await.is_err());
    }

    #[fuchsia::test]
    async fn test_read_fails_if_label_directory_is_empty() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = io_util::open_directory_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");
        let mut plt = PersistentLookupTable::new(dir);

        plt.write(&TEST_LABEL, b"foo bar".to_vec()).await.unwrap();
        let read_res = plt.read(&TEST_LABEL).await.unwrap();

        assert_eq!(read_res.bytes, b"foo bar".to_vec());
        assert_eq!(read_res.version, 1);

        // Manually delete files in the label directory.
        let dir_2 = io_util::open_directory_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");
        let child_dir = io_util::directory::create_directory(
            &dir_2,
            &TEST_LABEL.into_dir_name(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE,
        )
        .await
        .unwrap();
        for entry in files_async::readdir(&child_dir).await.unwrap() {
            child_dir.unlink(&entry.name, UnlinkOptions::EMPTY).await.unwrap().unwrap();
        }

        // Ensure that now we have an error since the label directory is empty.
        assert!(plt.read(&TEST_LABEL).await.is_err());
    }

    #[fuchsia::test]
    async fn test_multi_versioned_writes() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = io_util::open_directory_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
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
        let dir = io_util::open_directory_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
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
        let dir = io_util::open_directory_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
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
        let dir = io_util::open_directory_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");

        // Write a bad entry to the expected directory.
        let child_dir = io_util::directory::create_directory(
            &dir,
            &TEST_LABEL.into_dir_name(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE,
        )
        .await
        .unwrap();
        let bad_file = io_util::directory::open_file(
            &child_dir,
            &"bad file name",
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE,
        )
        .await
        .unwrap();
        bad_file
            .write(&b"foo bar 2".to_vec())
            .await
            .unwrap()
            .map_err(zx::Status::from_raw)
            .unwrap();
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
        let dir = io_util::open_directory_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");

        // Write a staged file to the label's directory.
        let child_dir = io_util::directory::create_directory(
            &dir,
            &TEST_LABEL.into_dir_name(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE,
        )
        .await
        .unwrap();
        let stale_file = io_util::directory::open_file(
            &child_dir,
            &format!("{}01234", STAGEDFILE_PREFIX),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE,
        )
        .await
        .unwrap();
        stale_file
            .write(&b"stale_file_content".to_vec())
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
        let entries = files_async::readdir(&child_dir).await.unwrap();
        assert!(entries.is_empty());

        // Check that we can write and read from the directory as expected.
        plt.write(&TEST_LABEL, b"foo bar".to_vec()).await.unwrap();
        let content = plt.read(&TEST_LABEL).await.unwrap();
        assert_eq!(content.bytes, b"foo bar".to_vec());
    }
}
