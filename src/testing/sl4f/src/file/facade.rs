// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use serde_json::{to_value, Value};
use std::{fs, io, path::Path};

use super::types::*;

/// Facade providing access to session testing interfaces.
#[derive(Debug)]
pub struct FileFacade;

impl FileFacade {
    pub fn new() -> Self {
        Self
    }

    /// Deletes the given path, which must be a file. Returns OK(NotFound) if the file does not
    /// exist.
    pub async fn delete_file(&self, args: Value) -> Result<DeleteFileResult, Error> {
        let path = args.get("path").ok_or(format_err!("DeleteFile failed, no path"))?;
        let path = path.as_str().ok_or(format_err!("DeleteFile failed, path not string"))?;

        match fs::remove_file(path) {
            Ok(()) => Ok(DeleteFileResult::Success),
            Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(DeleteFileResult::NotFound),
            Err(e) => Err(e.into()),
        }
    }

    /// Creates a new directory. Returns OK(AlreadyExists) if the directory already exists.
    pub async fn make_dir(&self, args: Value) -> Result<MakeDirResult, Error> {
        let path = args.get("path").ok_or(format_err!("MakeDir failed, no path"))?;
        let path = path.as_str().ok_or(format_err!("MakeDir failed, path not string"))?;
        let path = Path::new(path);

        let recurse = args["recurse"].as_bool().unwrap_or(false);

        if path.is_dir() {
            return Ok(MakeDirResult::AlreadyExists);
        }

        if recurse {
            fs::create_dir_all(path)?;
        } else {
            fs::create_dir(path)?;
        }
        Ok(MakeDirResult::Success)
    }

    /// Given a source file, fetches its contents.
    pub async fn read_file(&self, args: Value) -> Result<Value, Error> {
        let path = args.get("path").ok_or(format_err!("ReadFile failed, no path"))?;
        let path = path.as_str().ok_or(format_err!("ReadFile failed, path not string"))?;

        let contents = fs::read(path)?;
        let encoded_contents = base64::encode(&contents);

        Ok(to_value(encoded_contents)?)
    }

    /// Given data and the destination, it creates a new file and
    /// puts it in the corresponding path (given by the destination).
    pub async fn write_file(&self, args: Value) -> Result<WriteFileResult, Error> {
        let data = args.get("data").ok_or(format_err!("WriteFile failed, no data"))?;
        let data = data.as_str().ok_or(format_err!("WriteFile failed, data not string"))?;

        let contents = base64::decode(data)?;

        let destination =
            args.get("dst").ok_or(format_err!("WriteFile failed, no destination path given"))?;
        let destination =
            destination.as_str().ok_or(format_err!("WriteFile failed, destination not string"))?;

        fs::write(destination, &contents)?;
        Ok(WriteFileResult::Success)
    }

    /// Returns metadata for the given path. Returns Ok(NotFound) if the path does not exist.
    pub async fn stat(&self, args: Value) -> Result<StatResult, Error> {
        let path = args.get("path").ok_or(format_err!("Stat failed, no path"))?;
        let path = path.as_str().ok_or(format_err!("Stat failed, path not string"))?;

        let metadata = match fs::metadata(path) {
            Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(StatResult::NotFound),
            res => res,
        }?;

        Ok(StatResult::Success(Metadata {
            kind: if metadata.is_dir() {
                NodeKind::Directory
            } else if metadata.is_file() {
                NodeKind::File
            } else {
                NodeKind::Other
            },
            size: metadata.len(),
        }))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;
    use serde_json::json;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn delete_file_ok() {
        let temp = tempfile::tempdir().unwrap();
        let path = temp.path().join("test.txt");
        fs::write(&path, "hello world!".as_bytes()).unwrap();
        assert!(path.exists());

        assert_matches!(
            FileFacade.delete_file(json!({ "path": path })).await,
            Ok(DeleteFileResult::Success)
        );
        assert!(!path.exists());

        assert_matches!(
            FileFacade.delete_file(json!({ "path": path })).await,
            Ok(DeleteFileResult::NotFound)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn make_dir_ok() {
        let temp = tempfile::tempdir().unwrap();
        let path = temp.path().join("a");
        assert!(!path.exists());

        assert_matches!(
            FileFacade.make_dir(json!({ "path": path })).await,
            Ok(MakeDirResult::Success)
        );
        assert!(path.is_dir());

        assert_matches!(
            FileFacade.make_dir(json!({ "path": path })).await,
            Ok(MakeDirResult::AlreadyExists)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn make_dir_recurse_ok() {
        let temp = tempfile::tempdir().unwrap();
        let path = temp.path().join("a/b/c");
        assert!(!path.exists());

        assert_matches!(FileFacade.make_dir(json!({ "path": path })).await, Err(_));
        assert!(!path.exists());

        assert_matches!(
            FileFacade.make_dir(json!({ "path": path, "recurse": true })).await,
            Ok(MakeDirResult::Success)
        );
        assert!(path.is_dir());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn read_file_ok() {
        const FILE_CONTENTS: &str = "hello world!";
        const FILE_CONTENTS_AS_BASE64: &str = "aGVsbG8gd29ybGQh";

        let temp = tempfile::tempdir().unwrap();
        let path = temp.path().join("test.txt");
        fs::write(&path, FILE_CONTENTS.as_bytes()).unwrap();

        assert_matches!(
            FileFacade.read_file(json!({ "path": path })).await,
            Ok(value) if value == json!(FILE_CONTENTS_AS_BASE64)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_file_ok() {
        const FILE_CONTENTS: &str = "hello world!";
        const FILE_CONTENTS_AS_BASE64: &str = "aGVsbG8gd29ybGQh";

        let temp = tempfile::tempdir().unwrap();
        let path = temp.path().join("test.txt");

        assert_matches!(
            FileFacade.write_file(json!({ "data": FILE_CONTENTS_AS_BASE64, "dst": path })).await,
            Ok(WriteFileResult::Success)
        );

        assert_eq!(fs::read_to_string(&path).unwrap(), FILE_CONTENTS);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_file_unwritable_path() {
        const FILE_CONTENTS_AS_BASE64: &str = "aGVsbG8gd29ybGQh";

        assert_matches!(
            FileFacade
                .write_file(json!({ "data": FILE_CONTENTS_AS_BASE64, "dst": "/pkg/is/readonly" }))
                .await,
            Err(_)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn stat_file() {
        let temp = tempfile::tempdir().unwrap();
        let path = temp.path().join("test.txt");
        fs::write(&path, "hello world!".as_bytes()).unwrap();

        assert_matches!(
            FileFacade.stat(json!({ "path": path })).await,
            Ok(StatResult::Success(Metadata { kind: NodeKind::File, size: 12 }))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn stat_dir() {
        assert_matches!(
            FileFacade.stat(json!({ "path": "/pkg" })).await,
            Ok(StatResult::Success(Metadata { kind: NodeKind::Directory, size: 0 }))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn stat_not_found() {
        assert_matches!(
            FileFacade.stat(json!({ "path": "/the/ultimate/question" })).await,
            Ok(StatResult::NotFound)
        );
    }
}
