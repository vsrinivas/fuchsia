// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! fuchsia.IO UTIL-ity library
//!
//! This crate provides various helper functions for iteracting with
//! `fidl_fuchsia_io::{DirectoryProxy, FileProxy, NodeProxy}` objects.
//!
//! Functions in the top-level module are deprecated. New uses of `io_util` should use the
//! `directory`, `file`, or `node` modules instead.
//!
//! Functions that contain `in_namespace` in their name operate on absolute paths in the process's
//! current namespace and utilize a blocking `fdio` call to open the proxy.

use {
    anyhow::{format_err, Error},
    fidl::encoding::Persistable,
    fidl::endpoints::Proxy,
    fidl_fuchsia_io as fio,
    std::path::Path,
};

pub mod directory;
pub mod file;
pub mod node;

// Reexported from fidl_fuchsia_io for convenience
pub use fio::OpenFlags;

/// open_node will return a NodeProxy opened to the node at the given path relative to the
/// given directory, or return an error if no such node exists (or some other FIDL error was
/// encountered). This function will not block.
pub fn open_node<'a>(
    dir: &'a fio::DirectoryProxy,
    path: &'a Path,
    flags: fio::OpenFlags,
    mode: u32,
) -> Result<fio::NodeProxy, Error> {
    let path = check_path(path)?;
    let node = directory::open_node_no_describe(dir, path, flags, mode)?;
    Ok(node)
}

/// open_directory will open a NodeProxy at the given path relative to the given directory, and
/// convert it into a DirectoryProxy. This function will not block.
pub fn open_directory<'a>(
    dir: &'a fio::DirectoryProxy,
    path: &'a Path,
    flags: fio::OpenFlags,
) -> Result<fio::DirectoryProxy, Error> {
    let path = check_path(path)?;
    let node = directory::open_directory_no_describe(dir, path, flags)?;
    Ok(node)
}

/// open_file will open a NodeProxy at the given path relative to the given directory, and convert
/// it into a FileProxy. This function will not block.
pub fn open_file<'a>(
    dir: &'a fio::DirectoryProxy,
    path: &'a Path,
    flags: fio::OpenFlags,
) -> Result<fio::FileProxy, Error> {
    let path = check_path(path)?;
    let node = directory::open_file_no_describe(dir, path, flags)?;
    Ok(node)
}

pub async fn read_file(file: &fio::FileProxy) -> Result<String, Error> {
    let string = file::read_to_string(file).await?;
    Ok(string)
}

/// Write the given string as UTF-8 bytes into a file open for writing.
pub async fn write_file(file: &fio::FileProxy, data: &str) -> Result<(), Error> {
    file::write(file, data).await?;
    Ok(())
}

/// Write the given bytes into a file at `path`. The path must be an absolute path.
/// * If the file already exists, replaces existing contents.
/// * If the file does not exist, creates the file.
#[cfg(target_os = "fuchsia")]
pub async fn write_path_bytes(path: &str, data: &[u8]) -> Result<(), Error> {
    file::write_in_namespace(path, data).await?;
    Ok(())
}

/// Read the given FIDL message from binary form from a file open for reading.
/// FIDL structure should be provided at a read time.
/// Incompatible data is populated as per FIDL ABI compatibility guide:
/// https://fuchsia.dev/fuchsia-src/development/languages/fidl/guides/abi-compat
pub async fn read_file_fidl<T: Persistable>(file: &fio::FileProxy) -> Result<T, Error> {
    Ok(file::read_fidl(file).await?)
}

/// Read the given FIDL message from binary file at `path` in the current namespace. The path
/// must be an absolute path.
/// FIDL structure should be provided at a read time.
/// Incompatible data is populated as per FIDL ABI compatibility guide:
/// https://fuchsia.dev/fuchsia-src/development/languages/fidl/guides/abi-compat
#[cfg(target_os = "fuchsia")]
pub async fn read_path_fidl<T: Persistable>(path: &str) -> Result<T, Error> {
    Ok(file::read_in_namespace_to_fidl(path).await?)
}

/// Write the given FIDL message in a binary form into a file open for writing.
pub async fn write_file_fidl<T: Persistable>(
    file: &fio::FileProxy,
    data: &mut T,
) -> Result<(), Error> {
    file::write_fidl(file, data).await?;
    Ok(())
}

/// Write the given FIDL encoded message into a file at `path`. The path must be an absolute path.
/// * If the file already exists, replaces existing contents.
/// * If the file does not exist, creates the file.
#[cfg(target_os = "fuchsia")]
pub async fn write_path_fidl<T: Persistable>(path: &str, data: &mut T) -> Result<(), Error> {
    file::write_fidl_in_namespace(path, data).await?;
    Ok(())
}

/// node_to_directory will convert the given NodeProxy into a DirectoryProxy. This is unsafe if the
/// type of the node is not checked first.
pub fn node_to_directory(node: fio::NodeProxy) -> Result<fio::DirectoryProxy, Error> {
    let node_chan = node.into_channel().map_err(|e| format_err!("{:?}", e))?;
    Ok(fio::DirectoryProxy::from_channel(node_chan))
}

/// node_to_file will convert the given NodeProxy into a FileProxy. This is unsafe if the
/// type of the node is not checked first.
pub fn node_to_file(node: fio::NodeProxy) -> Result<fio::FileProxy, Error> {
    let node_chan = node.into_channel().map_err(|e| format_err!("{:?}", e))?;
    Ok(fio::FileProxy::from_channel(node_chan))
}

/// clone_directory will create a clone of the given DirectoryProxy by calling its clone function.
/// This function will not block.
pub fn clone_directory(
    dir: &fio::DirectoryProxy,
    flags: fio::OpenFlags,
) -> Result<fio::DirectoryProxy, Error> {
    let node = directory::clone_no_describe(dir, Some(flags))?;
    Ok(node)
}

/// canonicalize_path will remove a leading `/` if it exists, since it's always unnecessary and in
/// some cases disallowed (fxbug.dev/28436).
pub fn canonicalize_path(path: &str) -> &str {
    if path == "/" {
        return ".";
    }
    if path.starts_with('/') {
        return &path[1..];
    }
    path
}

/// Verifies path is relative, utf-8, and non-empty.
fn check_path(path: &Path) -> Result<&str, Error> {
    if path.is_absolute() {
        return Err(format_err!("path must be relative"));
    }
    let path = path.to_str().ok_or(format_err!("path contains invalid UTF-8"))?;
    if path.is_empty() {
        return Err(format_err!("path must not be empty"));
    }

    Ok(path)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::ServerEnd,
        fuchsia_async as fasync, fuchsia_zircon_status as zx_status,
        std::fs,
        tempfile::TempDir,
        vfs::{
            directory::entry::DirectoryEntry,
            execution_scope::ExecutionScope,
            file::vmo::{read_only_static, read_write, simple_init_vmo_with_capacity},
            pseudo_directory,
        },
    };

    #[fasync::run_singlethreaded(test)]
    async fn open_and_read_file_test() {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        let data = "abc".repeat(10000);
        fs::write(tempdir.path().join("myfile"), &data).expect("failed writing file");

        let dir = crate::directory::open_in_namespace(
            tempdir.path().to_str().unwrap(),
            OpenFlags::RIGHT_READABLE,
        )
        .expect("could not open tmp dir");
        let path = Path::new("myfile");
        let file = open_file(&dir, &path, OpenFlags::RIGHT_READABLE).expect("could not open file");
        let contents = read_file(&file).await.expect("could not read file");
        assert_eq!(&contents, &data, "File contents did not match");
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_and_write_file_test() {
        // Create temp dir for test.
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        let dir = crate::directory::open_in_namespace(
            tempdir.path().to_str().unwrap(),
            OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open tmp dir");

        // Write contents.
        let file_name = Path::new("myfile");
        let data = "abc".repeat(10000);
        let file = open_file(&dir, &file_name, OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::CREATE)
            .expect("could not open file");
        write_file(&file, &data).await.expect("could not write file");

        // Verify contents.
        let contents = std::fs::read_to_string(tempdir.path().join(file_name)).unwrap();
        assert_eq!(&contents, &data, "File contents did not match");
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_checks_path_validity() {
        let dir = crate::directory::open_in_namespace("/pkg", OpenFlags::RIGHT_READABLE)
            .expect("could not open /pkg");

        assert!(open_file(&dir, Path::new(""), OpenFlags::RIGHT_READABLE).is_err());
        assert!(open_file(&dir, Path::new("/"), OpenFlags::RIGHT_READABLE).is_err());
        assert!(open_file(&dir, Path::new("/foo"), OpenFlags::RIGHT_READABLE).is_err());
        assert!(open_directory(&dir, Path::new(""), OpenFlags::RIGHT_READABLE).is_err());
        assert!(open_directory(&dir, Path::new("/"), OpenFlags::RIGHT_READABLE).is_err());
        assert!(open_directory(&dir, Path::new("/foo"), OpenFlags::RIGHT_READABLE).is_err());
    }

    #[test]
    fn test_canonicalize_path() {
        assert_eq!(canonicalize_path("/"), ".");
        assert_eq!(canonicalize_path("/foo"), "foo");
        assert_eq!(canonicalize_path("/foo/bar/"), "foo/bar/");

        assert_eq!(canonicalize_path("."), ".");
        assert_eq!(canonicalize_path("./"), "./");
        assert_eq!(canonicalize_path("foo/bar/"), "foo/bar/");
    }

    #[fasync::run_until_stalled(test)]
    async fn flags_test() -> Result<(), Error> {
        let example_dir = pseudo_directory! {
            "read_only" => read_only_static("read_only"),
            "read_write" => read_write(
                simple_init_vmo_with_capacity("read_write".as_bytes(), 100)
            ),
        };
        let (example_dir_proxy, example_dir_service) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>()?;
        let scope = ExecutionScope::new();
        example_dir.open(
            scope,
            OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            vfs::path::Path::dot(),
            ServerEnd::new(example_dir_service.into_channel()),
        );

        for (file_name, flags, should_succeed) in vec![
            ("read_only", OpenFlags::RIGHT_READABLE, true),
            ("read_only", OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE, false),
            ("read_only", OpenFlags::RIGHT_WRITABLE, false),
            ("read_write", OpenFlags::RIGHT_READABLE, true),
            ("read_write", OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE, true),
            ("read_write", OpenFlags::RIGHT_WRITABLE, true),
        ] {
            let file_proxy = open_file(&example_dir_proxy, &Path::new(file_name), flags)?;
            match (should_succeed, file_proxy.query().await) {
                (true, Ok(_)) => (),
                (false, Err(_)) => continue,
                (true, Err(e)) => {
                    panic!("failed to open when expected success, couldn't describe: {:?}", e)
                }
                (false, Ok(d)) => {
                    panic!("successfully opened when expected failure, could describe: {:?}", d)
                }
            }
            if flags.intersects(OpenFlags::RIGHT_READABLE) {
                assert_eq!(file_name, read_file(&file_proxy).await.expect("failed to read file"));
            }
            if flags.intersects(OpenFlags::RIGHT_WRITABLE) {
                let _: u64 = file_proxy
                    .write(b"write_only")
                    .await
                    .expect("write failed")
                    .map_err(zx_status::Status::from_raw)
                    .expect("write error");
            }
            assert_eq!(file_proxy.close().await?, Ok(()));
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn write_path_bytes_create_test() {
        // Create temp dir for test, and bind it to our namespace.
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        let _dir = crate::directory::open_in_namespace(
            tempdir.path().to_str().unwrap(),
            OpenFlags::RIGHT_READABLE,
        )
        .expect("could not open tmp dir");
        let path = tempdir.path().join(Path::new("write_path_bytes_create"));
        let path_string = path.to_str().expect("converting path to string failed");

        // Write contents.
        let data = b"\x80"; // Non UTF-8 data: a continuation byte as the first byte.
        write_path_bytes(&path_string, data).await.expect("could not write to path");

        // Verify contents.
        let contents = std::fs::read(path).unwrap();
        assert_eq!(&contents, &data, "Contents did not match");
    }

    #[fasync::run_singlethreaded(test)]
    async fn write_path_bytes_replace_test() {
        // Create temp dir for test, and bind it to our namespace.
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        let _dir = crate::directory::open_in_namespace(
            tempdir.path().to_str().unwrap(),
            OpenFlags::RIGHT_READABLE,
        )
        .expect("could not open tmp dir");
        let path = tempdir.path().join(Path::new("write_path_bytes_replace"));
        let path_string = path.to_str().expect("converting path to string failed");

        // Write contents.
        let original_data = b"\x80\x81"; // Non UTF-8 data: a continuation byte as the first byte.
        write_path_bytes(&path_string, original_data).await.expect("could not write to path");

        // Over-write contents.
        let new_data = b"\x82"; // Non UTF-8 data: a continuation byte as the first byte.
        write_path_bytes(&path_string, new_data).await.expect("could not over-write to path");

        // Verify contents.
        let contents = std::fs::read(path).unwrap();
        assert_eq!(&contents, &new_data, "Contents did not match");
    }
}
