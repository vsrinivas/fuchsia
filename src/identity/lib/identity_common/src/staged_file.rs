// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
    rand::{thread_rng, Rng},
    tracing::warn,
};

const TEMPFILE_RANDOM_LENGTH: usize = 8usize;

/// Describes an error with StagedFile usage.
#[derive(thiserror::Error, Debug)]
pub enum StagedFileError {
    /// Invalid arguments.
    #[error("Invalid arguments to create a staged file: {0}")]
    InvalidArguments(String),

    /// Failed to open a file or directory.
    #[error("Failed to open: {0}")]
    OpenError(#[from] fuchsia_fs::node::OpenError),

    /// Failed during a FIDL call.
    #[error("Failed during FIDL call: {0}")]
    FidlError(#[from] fidl::Error),

    /// Failed to write to the staged file.
    #[error("Failed to write to backing storage: {0}")]
    WriteError(#[from] fuchsia_fs::file::WriteError),

    /// Failed to rename the staged file.
    #[error("Failed to rename temp file to target: {0}")]
    RenameError(#[from] fuchsia_fs::node::RenameError),

    /// Failed to flush data.
    #[error("Failed to flush to disk: {0}")]
    FlushError(#[source] zx::Status),

    /// Failed to close the staged file.
    #[error("Failed to close backing storage: {0}")]
    CloseError(#[source] zx::Status),

    /// Failed to readdir.
    #[error("Failed to readdir: {0}")]
    ReaddirError(#[from] fuchsia_fs::directory::Error),

    /// Failed to unlink file.
    #[error("Failed to unlink file: {0}")]
    UnlinkError(#[source] zx::Status),
}

/// StagedFile is a wrapper around a |&DirectoryProxy| and a |FileProxy|
/// for a staged file within that directory.
/// The primary purpose of StagedFile is to implement the atomic write workflow
/// summarized as open -> write -> sync -> close -> rename. This workflow is
/// simplified to simply write -> commit.
/// One caveat to the use of StagedFile is that in the event of power loss or
/// a crash, there may be orphaned temporary files in the directory.
/// This means that clients _should_ clean up their directories of temporary
/// files prior to operating in that directory. As such, it is important to
/// choose a |filename_prefix| that is guaranteed not to collide with
/// |target_filename|s given when calling StagedFile::commit.
/// It would have been preferable to use the tempfile crate here, but it lacks
/// the ability to open things without making use of paths and namespaces, and
/// as such, StagedFile should only be used in cases where we must supply our
/// own |DirectoryProxy|.
pub struct StagedFile<'a> {
    dir_proxy: &'a fio::DirectoryProxy,
    temp_filename: String,
    file_proxy: fio::FileProxy,
}

impl<'a> StagedFile<'a> {
    /// Creates a new instance of StagedFile bound to the lifetime of
    /// |dir_proxy| that respects |filename_prefix|.
    /// |filename_prefix| must have a length > 0.
    pub async fn new(
        dir_proxy: &'a fio::DirectoryProxy,
        tempfile_prefix: &str,
    ) -> Result<StagedFile<'a>, StagedFileError> {
        if tempfile_prefix.is_empty() {
            return Err(StagedFileError::InvalidArguments(String::from(
                "filename_prefix must not be empty",
            )));
        }
        let temp_filename = generate_tempfile_name(tempfile_prefix);
        let file_proxy = fuchsia_fs::directory::open_file(
            dir_proxy,
            &temp_filename,
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::CREATE,
        )
        .await?;

        Ok(StagedFile { dir_proxy, temp_filename, file_proxy })
    }

    /// Writes data to the backing staged file proxy.
    /// This file is not guaranteed to be persisted until commit is called,
    /// at which point it will be renamed to |target_filename|.
    pub async fn write(&mut self, data: &[u8]) -> Result<(), StagedFileError> {
        let () = fuchsia_fs::file::write(&self.file_proxy, data).await?;
        Ok(())
    }

    /// Commits the data in the staged file to |target_filename| via the
    /// traditional sync -> close -> rename atomic write workflow.
    /// Calling commit does not guarantee that |target_filename| will be
    /// available, but it does guarantee atomicity of the file if it does
    /// exist.
    pub async fn commit(self, target_filename: &str) -> Result<(), StagedFileError> {
        // Do the usual atomic commit via sync, close, and rename-to-target.
        // Stale files left by a crash should be cleaned up by calling cleanup_stale_files on the
        // next startup.
        let () = self
            .file_proxy
            .sync()
            .await?
            .map_err(zx::Status::from_raw)
            .map_err(StagedFileError::FlushError)?;
        let () = self
            .file_proxy
            .close()
            .await?
            .map_err(zx::Status::from_raw)
            .map_err(StagedFileError::CloseError)?;

        fuchsia_fs::directory::rename(self.dir_proxy, &self.temp_filename, target_filename)
            .await
            .map_err(StagedFileError::RenameError)?;
        Ok(())
    }

    /// Helper function to unlink files in a directory given a function that
    /// takes a filename and returns whether or not to unlink it.
    pub async fn cleanup_stale_files(
        dir_proxy: &fio::DirectoryProxy,
        tempfile_prefix: &str,
    ) -> Result<(), Vec<StagedFileError>> {
        let dirents_res = fuchsia_fs::directory::readdir(dir_proxy).await;
        let dirents = dirents_res.map_err(|err| vec![StagedFileError::ReaddirError(err)])?;
        let mut failures = Vec::new();

        for d in dirents.iter() {
            let name = &d.name;
            // For filenames that are known to be temporary, try to remove them.
            if name.starts_with(tempfile_prefix) {
                warn!("Removing unexpected file '{}' from directory", &name);
                let fidl_res = dir_proxy.unlink(name, fio::UnlinkOptions::EMPTY).await;
                match fidl_res {
                    Err(x) => failures.push(StagedFileError::FidlError(x)),
                    Ok(unlink_res) => {
                        if let Err(unlink_err) = unlink_res {
                            failures.push(StagedFileError::UnlinkError(zx::Status::from_raw(
                                unlink_err,
                            )));
                        }
                    }
                }
            }
        }

        if failures.is_empty() {
            Ok(())
        } else {
            Err(failures)
        }
    }
}

/// Generates a temporary filename using |thread_rng| to append random chars to
/// a given |prefix|.
fn generate_tempfile_name(prefix: &str) -> String {
    // Generate a tempfile with name "{prefix}-{random}"
    let mut buf = String::with_capacity(TEMPFILE_RANDOM_LENGTH + prefix.len() + 1);
    buf.push_str(prefix);
    buf.push('-');
    let mut rng = thread_rng();
    std::iter::repeat(())
        .map(|()| rng.sample(rand::distributions::Alphanumeric))
        .map(char::from)
        .take(TEMPFILE_RANDOM_LENGTH)
        .for_each(|c| buf.push(c));
    buf
}

#[cfg(test)]
mod test {
    use {super::*, tempfile::TempDir};

    #[fuchsia::test]
    async fn test_normal_flow() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = fuchsia_fs::directory::open_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");
        let mut staged_file = StagedFile::new(&dir, "prefix-").await.unwrap();
        staged_file.write(b"this is some file content".as_ref()).await.unwrap();
        staged_file.commit("target_file_01").await.unwrap();

        // Check that target_file_01 has been created.
        let open_res = fuchsia_fs::directory::open_file(
            &dir,
            "target_file_01",
            fio::OpenFlags::RIGHT_READABLE,
        )
        .await;
        assert!(open_res.is_ok());
        let file_bytes = fuchsia_fs::file::read(&open_res.unwrap()).await.unwrap();
        assert_eq!(file_bytes, b"this is some file content");
    }

    #[fuchsia::test]
    async fn test_empty_tempfile_prefix() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = fuchsia_fs::directory::open_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");
        assert!(StagedFile::new(&dir, "").await.is_err());
    }

    async fn write_test_file_content(dir_proxy: &fio::DirectoryProxy, filename: &str, data: &[u8]) {
        let file_proxy = fuchsia_fs::directory::open_file(
            dir_proxy,
            filename,
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::CREATE,
        )
        .await
        .expect("could not open test file");
        fuchsia_fs::file::write(&file_proxy, data).await.expect("could not write test file data")
    }

    async fn file_exists_with_data(
        dir_proxy: &fio::DirectoryProxy,
        filename: &str,
        expected_data: &[u8],
    ) -> bool {
        let file =
            fuchsia_fs::directory::open_file(dir_proxy, filename, fio::OpenFlags::RIGHT_READABLE)
                .await
                .expect("could not open file");
        let bytes = fuchsia_fs::file::read(&file).await.expect("could not read file data");
        expected_data == bytes
    }

    #[fuchsia::test]
    async fn test_cleanup_stale_files() {
        let tmp_dir = TempDir::new().unwrap();
        let dir = fuchsia_fs::directory::open_in_namespace(
            tmp_dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open temp dir");

        // Write a variety of staged and non-staged files to the directory.
        write_test_file_content(&dir, "staged-001", b"staged-001".as_ref()).await;
        write_test_file_content(&dir, "real-001", b"real-001".as_ref()).await;
        write_test_file_content(&dir, "staged-002", b"staged-002".as_ref()).await;
        write_test_file_content(&dir, "real-002", b"real-002".as_ref()).await;
        write_test_file_content(&dir, "staged-003", b"staged-003".as_ref()).await;
        write_test_file_content(&dir, "004", b"004".as_ref()).await;

        // Clean up stale files.
        StagedFile::cleanup_stale_files(&dir, "staged-").await.unwrap();

        // Ensure that only the non-staged files remain.
        let dirents = fuchsia_fs::directory::readdir(&dir).await.unwrap();
        assert_eq!(dirents.len(), 3);
        assert!(file_exists_with_data(&dir, "real-001", b"real-001".as_ref()).await);
        assert!(file_exists_with_data(&dir, "real-002", b"real-002".as_ref()).await);
        assert!(file_exists_with_data(&dir, "004", b"004".as_ref()).await);
    }

    #[test]
    fn test_generate_tempfile_name() {
        let name1 = generate_tempfile_name("temp-12345");
        let name2 = generate_tempfile_name("temp-12345");
        let prefix = "temp-12345-";
        assert!(name1.starts_with(prefix));
        assert!(name2.starts_with(prefix));
        assert_eq!(name1.len(), prefix.len() + TEMPFILE_RANDOM_LENGTH);
        assert_eq!(name2.len(), prefix.len() + TEMPFILE_RANDOM_LENGTH);
        assert_ne!(name1, name2);
    }
}
