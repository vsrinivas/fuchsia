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
    fidl::encoding::{Decodable, Encodable},
    fidl::endpoints::{create_proxy, Proxy, ServerEnd},
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, FileProxy, NodeProxy, MODE_TYPE_DIRECTORY,
        OPEN_FLAG_CREATE, OPEN_FLAG_DIRECTORY,
    },
    fuchsia_zircon as zx,
    std::path::{Component, Path},
};

pub mod directory;
pub mod file;
pub mod node;

// Reexported from fidl_fuchsia_io for convenience
pub const OPEN_RIGHT_READABLE: u32 = fidl_fuchsia_io::OPEN_RIGHT_READABLE;
pub const OPEN_RIGHT_WRITABLE: u32 = fidl_fuchsia_io::OPEN_RIGHT_WRITABLE;
pub const OPEN_RIGHT_EXECUTABLE: u32 = fidl_fuchsia_io::OPEN_RIGHT_EXECUTABLE;

/// open_node will return a NodeProxy opened to the node at the given path relative to the
/// given directory, or return an error if no such node exists (or some other FIDL error was
/// encountered). This function will not block.
pub fn open_node<'a>(
    dir: &'a DirectoryProxy,
    path: &'a Path,
    flags: u32,
    mode: u32,
) -> Result<NodeProxy, Error> {
    let path = check_path(path)?;
    let node = directory::open_node_no_describe(dir, path, flags, mode)?;
    Ok(node)
}

/// open_directory will open a NodeProxy at the given path relative to the given directory, and
/// convert it into a DirectoryProxy. This function will not block.
pub fn open_directory<'a>(
    dir: &'a DirectoryProxy,
    path: &'a Path,
    flags: u32,
) -> Result<DirectoryProxy, Error> {
    let path = check_path(path)?;
    let node = directory::open_directory_no_describe(dir, path, flags)?;
    Ok(node)
}

/// open_file will open a NodeProxy at the given path relative to the given directory, and convert
/// it into a FileProxy. This function will not block.
pub fn open_file<'a>(
    dir: &'a DirectoryProxy,
    path: &'a Path,
    flags: u32,
) -> Result<FileProxy, Error> {
    let path = check_path(path)?;
    let node = directory::open_file_no_describe(dir, path, flags)?;
    Ok(node)
}

/// # Panics
///
/// Panics if any path component of `path` is not a valid utf-8 encoded string.
pub fn create_sub_directories(
    root_dir: &DirectoryProxy,
    path: &Path,
) -> Result<DirectoryProxy, Error> {
    if path.components().next().is_none() {
        return Err(format_err!("path must not be empty"));
    }
    let mut dir = None;
    for part in path.components() {
        if let Component::Normal(part) = part {
            dir = Some({
                let dir_ref = match dir.as_ref() {
                    Some(r) => r,
                    None => root_dir,
                };
                let (subdir, local_server_end) = create_proxy::<DirectoryMarker>()?;
                dir_ref.open(
                    OPEN_FLAG_DIRECTORY
                        | OPEN_RIGHT_READABLE
                        | OPEN_RIGHT_WRITABLE
                        | OPEN_FLAG_CREATE,
                    MODE_TYPE_DIRECTORY,
                    part.to_str().unwrap(),
                    ServerEnd::new(local_server_end.into_channel()),
                )?;
                subdir
            });
        } else {
            return Err(format_err!("invalid item in path: {:?}", part));
        }
    }
    Ok(dir.unwrap())
}

// TODO: this function will block on the FDIO calls. This should be rewritten/wrapped/whatever to
// be asynchronous.

/// Connect a zx::Channel to a path in the namespace.
pub fn connect_in_namespace(
    path: &str,
    server_chan: zx::Channel,
    flags: u32,
) -> Result<(), zx::Status> {
    node::connect_in_namespace(path, flags, server_chan)
}

/// open_node_in_namespace will return a NodeProxy to the given path by using the default namespace
/// stored in fdio. The path argument must be an absolute path.
pub fn open_node_in_namespace(path: &str, flags: u32) -> Result<NodeProxy, Error> {
    let node = node::open_in_namespace(path, flags)?;
    Ok(node)
}

/// open_directory_in_namespace will open a NodeProxy to the given path and convert it into a
/// DirectoryProxy. The path argument must be an absolute path.
pub fn open_directory_in_namespace(path: &str, flags: u32) -> Result<DirectoryProxy, Error> {
    let node = directory::open_in_namespace(path, flags)?;
    Ok(node)
}

/// open_file_in_namespace will open a NodeProxy to the given path and convert it into a FileProxy.
/// The path argument must be an absolute path.
pub fn open_file_in_namespace(path: &str, flags: u32) -> Result<FileProxy, Error> {
    let node = file::open_in_namespace(path, flags)?;
    Ok(node)
}

pub async fn read_file_bytes(file: &FileProxy) -> Result<Vec<u8>, Error> {
    let bytes = file::read(file).await?;
    Ok(bytes)
}

pub async fn read_file(file: &FileProxy) -> Result<String, Error> {
    let string = file::read_to_string(file).await?;
    Ok(string)
}

/// Write the given bytes into a file open for writing.
pub async fn write_file_bytes(file: &FileProxy, data: &[u8]) -> Result<(), Error> {
    file::write(file, data).await?;
    Ok(())
}

/// Write the given string as UTF-8 bytes into a file open for writing.
pub async fn write_file(file: &FileProxy, data: &str) -> Result<(), Error> {
    file::write(file, data).await?;
    Ok(())
}

/// Write the given bytes into a file at `path`. The path must be an absolute path.
/// * If the file already exists, replaces existing contents.
/// * If the file does not exist, creates the file.
pub async fn write_path_bytes(path: &str, data: &[u8]) -> Result<(), Error> {
    file::write_in_namespace(path, data).await?;
    Ok(())
}

/// Read the given FIDL message from binary form from a file open for reading.
/// FIDL structure should be provided at a read time.
/// Incompatible data is populated as per FIDL ABI compatibility guide:
/// https://fuchsia.dev/fuchsia-src/development/languages/fidl/guides/abi-compat
pub async fn read_file_fidl<T: Decodable>(file: &FileProxy) -> Result<T, Error> {
    Ok(file::read_fidl(file).await?)
}

/// Read the given FIDL message from binary file at `path` in the current namespace. The path
/// must be an absolute path.
/// FIDL structure should be provided at a read time.
/// Incompatible data is populated as per FIDL ABI compatibility guide:
/// https://fuchsia.dev/fuchsia-src/development/languages/fidl/guides/abi-compat
pub async fn read_path_fidl<T: Decodable>(path: &str) -> Result<T, Error> {
    Ok(file::read_in_namespace_to_fidl(path).await?)
}

/// Write the given FIDL message in a binary form into a file open for writing.
pub async fn write_file_fidl<T: Encodable>(file: &FileProxy, data: &mut T) -> Result<(), Error> {
    file::write_fidl(file, data).await?;
    Ok(())
}

/// Write the given FIDL encoded message into a file at `path`. The path must be an absolute path.
/// * If the file already exists, replaces existing contents.
/// * If the file does not exist, creates the file.
pub async fn write_path_fidl<T: Encodable>(path: &str, data: &mut T) -> Result<(), Error> {
    file::write_fidl_in_namespace(path, data).await?;
    Ok(())
}

/// node_to_directory will convert the given NodeProxy into a DirectoryProxy. This is unsafe if the
/// type of the node is not checked first.
pub fn node_to_directory(node: NodeProxy) -> Result<DirectoryProxy, Error> {
    let node_chan = node.into_channel().map_err(|e| format_err!("{:?}", e))?;
    Ok(DirectoryProxy::from_channel(node_chan))
}

/// node_to_file will convert the given NodeProxy into a FileProxy. This is unsafe if the
/// type of the node is not checked first.
pub fn node_to_file(node: NodeProxy) -> Result<FileProxy, Error> {
    let node_chan = node.into_channel().map_err(|e| format_err!("{:?}", e))?;
    Ok(FileProxy::from_channel(node_chan))
}

/// clone_directory will create a clone of the given DirectoryProxy by calling its clone function.
/// This function will not block.
pub fn clone_directory(dir: &DirectoryProxy, flags: u32) -> Result<DirectoryProxy, Error> {
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
        fidl::endpoints::{Proxy, ServerEnd},
        fidl_fuchsia_io::DirectoryMarker,
        fuchsia_async as fasync,
        futures::future,
        std::fs,
        tempfile::{NamedTempFile, TempDir},
        vfs::{
            directory::entry::DirectoryEntry,
            execution_scope::ExecutionScope,
            file::pcb::{read_only_static, read_write, write_only},
            pseudo_directory,
        },
    };

    #[fasync::run_singlethreaded(test)]
    async fn open_and_read_file_test() {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        let data = "abc".repeat(10000);
        fs::write(tempdir.path().join("myfile"), &data).expect("failed writing file");

        let dir =
            open_directory_in_namespace(tempdir.path().to_str().unwrap(), OPEN_RIGHT_READABLE)
                .expect("could not open tmp dir");
        let path = Path::new("myfile");
        let file = open_file(&dir, &path, OPEN_RIGHT_READABLE).expect("could not open file");
        let contents = read_file(&file).await.expect("could not read file");
        assert_eq!(&contents, &data, "File contents did not match");
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_and_write_file_test() {
        // Create temp dir for test.
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        let dir = open_directory_in_namespace(
            tempdir.path().to_str().unwrap(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .expect("could not open tmp dir");

        // Write contents.
        let file_name = Path::new("myfile");
        let data = "abc".repeat(10000);
        let file = open_file(&dir, &file_name, OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE)
            .expect("could not open file");
        write_file(&file, &data).await.expect("could not write file");

        // Verify contents.
        let contents = std::fs::read_to_string(tempdir.path().join(file_name)).unwrap();
        assert_eq!(&contents, &data, "File contents did not match");
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_checks_path_validity() {
        let dir =
            open_directory_in_namespace("/pkg", OPEN_RIGHT_READABLE).expect("could not open /pkg");

        assert!(open_file(&dir, Path::new(""), OPEN_RIGHT_READABLE).is_err());
        assert!(open_file(&dir, Path::new("/"), OPEN_RIGHT_READABLE).is_err());
        assert!(open_file(&dir, Path::new("/foo"), OPEN_RIGHT_READABLE).is_err());
        assert!(open_directory(&dir, Path::new(""), OPEN_RIGHT_READABLE).is_err());
        assert!(open_directory(&dir, Path::new("/"), OPEN_RIGHT_READABLE).is_err());
        assert!(open_directory(&dir, Path::new("/foo"), OPEN_RIGHT_READABLE).is_err());
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
                || future::ok("read_write".as_bytes().into()),
                100,
                |_| future::ok(()),
            ),
            "write_only" => write_only(100, |_| future::ok(())),
        };
        let (example_dir_proxy, example_dir_service) =
            fidl::endpoints::create_proxy::<DirectoryMarker>()?;
        let scope = ExecutionScope::new();
        example_dir.open(
            scope,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            vfs::path::Path::empty(),
            ServerEnd::new(example_dir_service.into_channel()),
        );

        for (file_name, flags, should_succeed) in vec![
            ("read_only", OPEN_RIGHT_READABLE, true),
            ("read_only", OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, false),
            ("read_only", OPEN_RIGHT_WRITABLE, false),
            ("read_write", OPEN_RIGHT_READABLE, true),
            ("read_write", OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, true),
            ("read_write", OPEN_RIGHT_WRITABLE, true),
            ("write_only", OPEN_RIGHT_READABLE, false),
            ("write_only", OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, false),
            ("write_only", OPEN_RIGHT_WRITABLE, true),
        ] {
            let file_proxy = open_file(&example_dir_proxy, &Path::new(file_name), flags)?;
            match (should_succeed, file_proxy.describe().await) {
                (true, Ok(_)) => (),
                (false, Err(_)) => continue,
                (true, Err(e)) => {
                    panic!("failed to open when expected success, couldn't describe: {:?}", e)
                }
                (false, Ok(d)) => {
                    panic!("successfully opened when expected failure, could describe: {:?}", d)
                }
            }
            if flags & OPEN_RIGHT_READABLE != 0 {
                assert_eq!(file_name, read_file(&file_proxy).await.expect("failed to read file"));
            }
            if flags & OPEN_RIGHT_WRITABLE != 0 {
                let (s, _) = file_proxy.write(b"write_only").await?;
                assert_eq!(zx::Status::OK, zx::Status::from_raw(s));
            }
            assert_eq!(zx::Status::OK, zx::Status::from_raw(file_proxy.close().await?));
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_directory_in_namespace_rejects_files() {
        let tempfile = NamedTempFile::new().expect("failed to create tmp file");
        let dir =
            open_directory_in_namespace(tempfile.path().to_str().unwrap(), OPEN_RIGHT_READABLE)
                .expect("could not send open request");

        // We should see a PEER_CLOSED because we tried to open a file as a directory
        dir.on_closed().await.expect("Error waiting for peer closed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_sub_directories_test() -> Result<(), Error> {
        let tempdir = TempDir::new()?;

        let path = Path::new("path/to/example/dir");
        let file_name = Path::new("example_file_name");
        let data = "file contents";

        let root_dir = open_directory_in_namespace(
            tempdir.path().to_str().unwrap(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )?;

        let sub_dir = create_sub_directories(&root_dir, &path)?;
        let file = open_file(
            &sub_dir,
            &file_name,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE,
        )?;

        write_file(&file, &data).await.expect("writing to the file failed");

        let contents = std::fs::read_to_string(tempdir.path().join(path).join(file_name))?;
        assert_eq!(&contents, &data, "File contents did not match");

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn write_path_bytes_create_test() {
        // Create temp dir for test, and bind it to our namespace.
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        let _dir =
            open_directory_in_namespace(tempdir.path().to_str().unwrap(), OPEN_RIGHT_READABLE)
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
        let _dir =
            open_directory_in_namespace(tempdir.path().to_str().unwrap(), OPEN_RIGHT_READABLE)
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
