// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(await_macro, async_await)]

use {
    failure::{err_msg, format_err, Error},
    fdio,
    fidl::endpoints::create_proxy,
    fidl::endpoints::Proxy,
    fidl_fuchsia_io::{
        DirectoryProxy, FileProxy, NodeProxy, MAX_BUF, MODE_TYPE_DIRECTORY, MODE_TYPE_FILE,
        OPEN_FLAG_DESCRIBE, OPEN_RIGHT_READABLE,
    },
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, HandleBased},
    std::ffi::CString,
    std::path::PathBuf,
    std::ptr,
    std::str::from_utf8,
};

/// open_node will return a NodeProxy opened to the node at the given path relative to the
/// given directory, or return an error if no such node exists (or some other FIDL error was
/// encountered). This function will not block.
pub fn open_node<'a>(
    dir: &'a DirectoryProxy,
    path: &'a PathBuf,
    mode: u32,
) -> Result<NodeProxy, Error> {
    let path = path.to_str().ok_or(err_msg("path is invalid"))?;
    let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
    let (new_node, server_end) = create_proxy()?;
    dir.open(flags, mode, path, server_end)?;
    Ok(new_node)
}

/// open_directory will open a NodeProxy at the given path relative to the given directory, and
/// convert it into a DirectoryProxy. This function will not block.
pub fn open_directory<'a>(
    dir: &'a DirectoryProxy,
    path: &'a PathBuf,
) -> Result<DirectoryProxy, Error> {
    node_to_directory(open_node(dir, path, MODE_TYPE_DIRECTORY)?)
}

/// open_file will open a NodeProxy at the given path relative to the given directory, and convert
/// it into a FileProxy. This function will not block.
pub fn open_file<'a>(dir: &'a DirectoryProxy, path: &'a PathBuf) -> Result<FileProxy, Error> {
    node_to_file(open_node(dir, path, MODE_TYPE_FILE)?)
}

// TODO: this function will block on the FDIO calls. This should be rewritten/wrapped/whatever to
// be asynchronous.

/// Connect a zx::Channel to a path in the namespace.
pub fn connect_in_namespace(path: &str, server_chan: zx::Channel) -> Result<(), zx::Status> {
    let mut ns_ptr: *mut fdio::fdio_sys::fdio_ns_t = ptr::null_mut();
    let status = unsafe { fdio::fdio_sys::fdio_ns_get_installed(&mut ns_ptr) };
    if status != zx::sys::ZX_OK {
        return Err(zx::Status::from_raw(status));
    }

    let cstr = CString::new(path)?;
    let status = unsafe {
        fdio::fdio_sys::fdio_ns_connect(
            ns_ptr,
            cstr.as_ptr(),
            OPEN_RIGHT_READABLE,
            server_chan.into_raw(),
        )
    };
    if status != zx::sys::ZX_OK {
        return Err(zx::Status::from_raw(status));
    }
    Ok(())
}

/// open_node_in_namespace will return a NodeProxy to the given path by using the default namespace
/// stored in fdio. The path argument must be an absolute path.
pub fn open_node_in_namespace(path: &str) -> Result<NodeProxy, Error> {
    let mut ns_ptr: *mut fdio::fdio_sys::fdio_ns_t = ptr::null_mut();
    let status = unsafe { fdio::fdio_sys::fdio_ns_get_installed(&mut ns_ptr) };
    if status != zx::sys::ZX_OK {
        return Err(format_err!("fdio_ns_get_installed error: {}", status));
    }

    let (proxy_chan, server_end) = zx::Channel::create()
        .map_err(|status| format_err!("zx::Channel::create error: {}", status))?;

    let cstr = CString::new(path)?;
    let status = unsafe {
        fdio::fdio_sys::fdio_ns_connect(
            ns_ptr,
            cstr.as_ptr(),
            OPEN_RIGHT_READABLE,
            server_end.into_raw(),
        )
    };
    if status != zx::sys::ZX_OK {
        return Err(format_err!("fdio_ns_connect error: {}", status));
    }

    return Ok(NodeProxy::new(fasync::Channel::from_channel(proxy_chan)?));
}

/// open_directory_in_namespace will open a NodeProxy to the given path and convert it into a
/// DirectoryProxy. The path argument must be an absolute path.
pub fn open_directory_in_namespace(path: &str) -> Result<DirectoryProxy, Error> {
    node_to_directory(open_node_in_namespace(path)?)
}

/// open_file_in_namespace will open a NodeProxy to the given path and convert it into a FileProxy.
/// The path argument must be an absolute path.
pub fn open_file_in_namespace(path: &str) -> Result<FileProxy, Error> {
    node_to_file(open_node_in_namespace(path)?)
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
pub fn clone_directory(dir: &DirectoryProxy) -> Result<DirectoryProxy, Error> {
    let (node_clone, server_end) = create_proxy()?;
    dir.clone(OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE, server_end)?;
    node_to_directory(node_clone)
}

/// canonicalize_path will remove a leading `/` if it exists, since it's always unnecessary and in
/// some cases disallowed (US-569).
pub fn canonicalize_path(path: &str) -> String {
    let mut res = path.to_string();
    if res.starts_with("/") {
        res.remove(0);
    }
    res
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync, std::fs, tempfile::TempDir};

    #[test]
    fn open_and_read_file_test() {
        let mut executor = fasync::Executor::new().unwrap();
        executor.run_singlethreaded(
            async {
                let tempdir = TempDir::new().expect("failed to create tmp dir");
                let data = "abc".repeat(10000);
                fs::write(tempdir.path().join("myfile"), &data).expect("failed writing file");

                let dir = open_directory_in_namespace(tempdir.path().to_str().unwrap())
                    .expect("could not open tmp dir");
                let path = PathBuf::from("myfile");
                let file = open_file(&dir, &path).expect("could not open file");
                let contents = await!(read_file(&file)).expect("could not read file");
                assert_eq!(&contents, &data, "File contents did not match");
            },
        );
    }
}
