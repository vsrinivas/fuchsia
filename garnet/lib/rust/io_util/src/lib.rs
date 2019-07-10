// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(await_macro, async_await)]

use {
    failure::{err_msg, format_err, Error},
    fdio,
    fidl::endpoints::Proxy,
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, FileProxy, NodeProxy, MAX_BUF, MODE_TYPE_DIRECTORY,
        MODE_TYPE_FILE, OPEN_FLAG_CREATE, OPEN_FLAG_DIRECTORY,
    },
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, HandleBased},
    std::ffi::CString,
    std::path::{Component, Path},
    std::ptr,
    std::str::from_utf8,
};

// Reexported from fidl_fuchsia_io for convenience
pub const OPEN_RIGHT_READABLE: u32 = fidl_fuchsia_io::OPEN_RIGHT_READABLE;
pub const OPEN_RIGHT_WRITABLE: u32 = fidl_fuchsia_io::OPEN_RIGHT_WRITABLE;

/// open_node will return a NodeProxy opened to the node at the given path relative to the
/// given directory, or return an error if no such node exists (or some other FIDL error was
/// encountered). This function will not block.
pub fn open_node<'a>(
    dir: &'a DirectoryProxy,
    path: &'a Path,
    flags: u32,
    mode: u32,
) -> Result<NodeProxy, Error> {
    let path = path.to_str().ok_or(err_msg("path is invalid"))?;
    let (new_node, server_end) = create_proxy()?;
    dir.open(flags, mode, path, server_end)?;
    Ok(new_node)
}

/// open_directory will open a NodeProxy at the given path relative to the given directory, and
/// convert it into a DirectoryProxy. This function will not block.
pub fn open_directory<'a>(
    dir: &'a DirectoryProxy,
    path: &'a Path,
    flags: u32,
) -> Result<DirectoryProxy, Error> {
    node_to_directory(open_node(dir, path, flags | OPEN_FLAG_DIRECTORY, MODE_TYPE_DIRECTORY)?)
}

/// open_file will open a NodeProxy at the given path relative to the given directory, and convert
/// it into a FileProxy. This function will not block.
pub fn open_file<'a>(
    dir: &'a DirectoryProxy,
    path: &'a Path,
    flags: u32,
) -> Result<FileProxy, Error> {
    node_to_file(open_node(dir, path, flags, MODE_TYPE_FILE)?)
}

pub fn create_sub_directories(
    mut dir: DirectoryProxy,
    path: &Path,
) -> Result<DirectoryProxy, Error> {
    for part in path.components() {
        if let Component::Normal(part) = part {
            let (sub_dir_proxy, local_server_end) = create_proxy::<DirectoryMarker>()?;
            dir.open(
                OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE,
                MODE_TYPE_DIRECTORY,
                part.to_str().unwrap(),
                ServerEnd::new(local_server_end.into_channel()),
            )?;
            dir = sub_dir_proxy;
        } else {
            return Err(format_err!("invalid item in path: {:?}", part));
        }
    }
    Ok(dir)
}

// TODO: this function will block on the FDIO calls. This should be rewritten/wrapped/whatever to
// be asynchronous.

/// Connect a zx::Channel to a path in the namespace.
pub fn connect_in_namespace(
    path: &str,
    server_chan: zx::Channel,
    flags: u32,
) -> Result<(), zx::Status> {
    let mut ns_ptr: *mut fdio::fdio_sys::fdio_ns_t = ptr::null_mut();
    let status = unsafe { fdio::fdio_sys::fdio_ns_get_installed(&mut ns_ptr) };
    if status != zx::sys::ZX_OK {
        return Err(zx::Status::from_raw(status));
    }

    let cstr = CString::new(path)?;
    let status = unsafe {
        fdio::fdio_sys::fdio_ns_connect(ns_ptr, cstr.as_ptr(), flags, server_chan.into_raw())
    };
    if status != zx::sys::ZX_OK {
        return Err(zx::Status::from_raw(status));
    }
    Ok(())
}

/// open_node_in_namespace will return a NodeProxy to the given path by using the default namespace
/// stored in fdio. The path argument must be an absolute path.
pub fn open_node_in_namespace(path: &str, flags: u32) -> Result<NodeProxy, Error> {
    let mut ns_ptr: *mut fdio::fdio_sys::fdio_ns_t = ptr::null_mut();
    let status = unsafe { fdio::fdio_sys::fdio_ns_get_installed(&mut ns_ptr) };
    if status != zx::sys::ZX_OK {
        return Err(format_err!("fdio_ns_get_installed error: {}", status));
    }

    let (proxy_chan, server_end) = zx::Channel::create()
        .map_err(|status| format_err!("zx::Channel::create error: {}", status))?;

    let cstr = CString::new(path)?;
    let status = unsafe {
        fdio::fdio_sys::fdio_ns_connect(ns_ptr, cstr.as_ptr(), flags, server_end.into_raw())
    };
    if status != zx::sys::ZX_OK {
        return Err(format_err!("fdio_ns_connect error: {}", status));
    }

    return Ok(NodeProxy::new(fasync::Channel::from_channel(proxy_chan)?));
}

/// open_directory_in_namespace will open a NodeProxy to the given path and convert it into a
/// DirectoryProxy. The path argument must be an absolute path.
pub fn open_directory_in_namespace(path: &str, flags: u32) -> Result<DirectoryProxy, Error> {
    node_to_directory(open_node_in_namespace(path, flags)?)
}

/// open_file_in_namespace will open a NodeProxy to the given path and convert it into a FileProxy.
/// The path argument must be an absolute path.
pub fn open_file_in_namespace(path: &str, flags: u32) -> Result<FileProxy, Error> {
    node_to_file(open_node_in_namespace(path, flags)?)
}

pub async fn read_file(file: &FileProxy) -> Result<String, Error> {
    let mut out = String::new();
    loop {
        let (status, bytes) = await!(file.read(MAX_BUF)).map_err(|e| Error::from(e))?;
        let status = zx::Status::from_raw(status);
        if status != zx::Status::OK {
            return Err(format_err!("failed to read file: {}", status));
        }
        if bytes.is_empty() {
            break;
        }
        out.push_str(from_utf8(&bytes).map_err(|e| Error::from(e))?);
    }
    Ok(out)
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
    let (node_clone, server_end) = create_proxy()?;
    dir.clone(flags, server_end)?;
    node_to_directory(node_clone)
}

/// canonicalize_path will remove a leading `/` if it exists, since it's always unnecessary and in
/// some cases disallowed (US-569).
pub fn canonicalize_path(path: &str) -> &str {
    if path.starts_with('/') {
        return &path[1..];
    }
    path
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_io::DirectoryMarker,
        fuchsia_async as fasync,
        fuchsia_vfs_pseudo_fs::{
            directory::entry::DirectoryEntry,
            file::simple::{read_only_str, read_write_str, write_only},
            pseudo_directory,
        },
        std::fs,
        std::iter,
        tempfile::TempDir,
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
        let contents = await!(read_file(&file)).expect("could not read file");
        assert_eq!(&contents, &data, "File contents did not match");
    }

    #[fasync::run_until_stalled(test)]
    async fn flags_test() -> Result<(), Error> {
        let mut example_dir = pseudo_directory! {
            "read_only" => read_only_str(|| Ok("read_only".to_string())),
            "read_write" => read_write_str(
                || Ok("read_write".to_string()),
                100,
                |_| Ok(()),
            ),
            "write_only" => write_only(100, |_| Ok(())),
        };
        let (example_dir_proxy, example_dir_service) =
            fidl::endpoints::create_proxy::<DirectoryMarker>()?;
        example_dir.open(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            &mut iter::empty(),
            ServerEnd::new(example_dir_service.into_channel()),
        );
        fasync::spawn(async move {
            let _ = await!(example_dir);
        });

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
            match (should_succeed, await!(file_proxy.describe())) {
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
                assert_eq!(file_name, await!(read_file(&file_proxy)).expect("failed to read file"));
            }
            if flags & OPEN_RIGHT_WRITABLE != 0 {
                let (s, _) = await!(file_proxy.write(&mut b"write_only".to_vec().into_iter()))?;
                assert_eq!(zx::Status::OK, zx::Status::from_raw(s));
            }
            assert_eq!(zx::Status::OK, zx::Status::from_raw(await!(file_proxy.close())?));
        }
        Ok(())
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

        let sub_dir = create_sub_directories(
            clone_directory(&root_dir, fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS)?,
            &path,
        )?;
        let file = open_file(
            &sub_dir,
            &file_name,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE,
        )?;

        let (s, _) = await!(file.write(&mut data.as_bytes().to_vec().into_iter()))?;
        assert_eq!(zx::Status::OK, zx::Status::from_raw(s), "writing to the file failed");

        let contents = std::fs::read_to_string(tempdir.path().join(path).join(file_name))?;
        assert_eq!(&contents, &data, "File contents did not match");

        Ok(())
    }
}
