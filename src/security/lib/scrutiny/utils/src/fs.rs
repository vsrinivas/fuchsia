// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    once_cell::sync::Lazy,
    std::{
        fs::{create_dir_all, remove_dir_all},
        path::{Path, PathBuf},
        process::id,
        sync::atomic::{AtomicU32, Ordering},
    },
    tempfile::TempDir,
    tracing::warn,
};

static PID: Lazy<u32> = Lazy::new(|| id());
static NEXT_DIR_ID: AtomicU32 = AtomicU32::new(0);

/// A temporary directory that will be deleted when this object goes out of scope.
pub enum TemporaryDirectory {
    /// A directory backed by a `tempfile::TempDir`.
    TempDir(TempDir),
    /// A temporary directory managed by the `TemporaryDirectory` implementation.
    ManagedDir(PathBuf),
}

impl TemporaryDirectory {
    /// Borrow a `Path` that refers to this temporary directory.
    pub fn as_path(&self) -> &Path {
        match self {
            Self::TempDir(temp_dir) => temp_dir.path(),
            Self::ManagedDir(path_buf) => path_buf.as_path(),
        }
    }
}

impl From<TempDir> for TemporaryDirectory {
    fn from(temp_dir: TempDir) -> Self {
        Self::TempDir(temp_dir)
    }
}

/// Create a new temporary directory. If `root` is non-None, create a subdirectory under `root`,
/// otherwise delegate to `tempfile::tempdir()` to select a directory path. Fail if either
/// `create_dir_all()` or `tempfile::tempdir()` fails to create a suitable directory.
pub fn tempdir<P: AsRef<Path>>(root: Option<P>) -> Result<TemporaryDirectory> {
    match root {
        Some(root) => {
            let directory_name =
                format!("scrutiny-tmp-{}-{}", *PID, NEXT_DIR_ID.fetch_add(1, Ordering::SeqCst));
            let directory_path = root.as_ref().join(&directory_name);
            create_dir_all(&directory_path).map_err(|err| {
                anyhow!("Failed to create temporary directory at {:?}: {}", &directory_path, err)
            })?;
            Ok(TemporaryDirectory::ManagedDir(directory_path))
        }
        None => {
            let temp_dir = tempfile::tempdir().context("Failed to create tempfile::TempDir")?;
            Ok(TemporaryDirectory::TempDir(temp_dir))
        }
    }
}

impl Drop for TemporaryDirectory {
    /// Make a best effort to delete the temporary directory in the case where the directory is
    /// managed by this object. Log a warning if directory deletion fails.
    fn drop(&mut self) {
        if let Self::ManagedDir(directory_path) = self {
            if remove_dir_all(&directory_path).is_err() {
                warn!(path = ?directory_path, "Failed to delete temporary directory");
            }
        }
    }
}

#[cfg(test)]
mod test {
    use {
        super::tempdir,
        std::{
            fs::{metadata, remove_dir_all},
            path::PathBuf,
        },
    };

    #[fuchsia::test]
    fn tempfile_tempdir_smoke_test() {
        let tmp_dir_path_buf = {
            let temporary_directory = tempdir::<PathBuf>(None).unwrap();
            let tmp_dir_path = temporary_directory.as_path();
            assert!(metadata(tmp_dir_path).is_ok());
            tmp_dir_path.to_path_buf()
        };
        assert!(metadata(&tmp_dir_path_buf).is_err());
    }

    #[fuchsia::test]
    fn specific_tempdir_smoke_test() {
        let tempfile_tempdir = tempfile::tempdir().unwrap();
        let tmp_dir_path_buf = {
            let temporary_directory = tempdir(Some(tempfile_tempdir.path())).unwrap();
            let tmp_dir_path = temporary_directory.as_path();
            assert!(tmp_dir_path.starts_with(tempfile_tempdir.path()));
            assert!(metadata(tmp_dir_path).is_ok());
            tmp_dir_path.to_path_buf()
        };
        assert!(metadata(&tmp_dir_path_buf).is_err());
    }

    #[fuchsia::test]
    fn specific_tempdir_unique_names() {
        let tempfile_tempdir = tempfile::tempdir().unwrap();
        let temp_dirs = (
            tempdir(Some(tempfile_tempdir.path())).unwrap(),
            tempdir(Some(tempfile_tempdir.path())).unwrap(),
        );
        assert_eq!(temp_dirs.0.as_path(), temp_dirs.0.as_path());
        assert!(temp_dirs.0.as_path() != temp_dirs.1.as_path());
    }

    #[fuchsia::test]
    fn tempfile_tempdir_out_of_order_removal() {
        let tmp_dir_path_buf = {
            let temporary_directory = tempdir::<PathBuf>(None).unwrap();
            let tmp_dir_path = temporary_directory.as_path();
            assert!(metadata(tmp_dir_path).is_ok());
            remove_dir_all(tmp_dir_path).unwrap();
            tmp_dir_path.to_path_buf()
        };
        assert!(metadata(&tmp_dir_path_buf).is_err());
    }

    #[fuchsia::test]
    fn specific_tempdir_out_of_order_removal() {
        let tempfile_tempdir = tempfile::tempdir().unwrap();
        let tmp_dir_path_buf = {
            let temporary_directory = tempdir(Some(tempfile_tempdir.path())).unwrap();
            let tmp_dir_path = temporary_directory.as_path();
            assert!(tmp_dir_path.starts_with(tempfile_tempdir.path()));
            assert!(metadata(tmp_dir_path).is_ok());
            remove_dir_all(tmp_dir_path).unwrap();
            tmp_dir_path.to_path_buf()
        };
        assert!(metadata(&tmp_dir_path_buf).is_err());
    }
}
