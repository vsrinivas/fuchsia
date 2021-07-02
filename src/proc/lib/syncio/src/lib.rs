// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::ServerEnd;
use fidl_fuchsia_io as fio;
use fuchsia_zircon as zx;

mod zxio;

// TODO: We need a more comprehensive error strategy.
// Our dependencies create elaborate error objects, but Starnix would prefer
// this library produce zx::Status errors for easier conversion to Errno.

/// A fuchsia.io.Node along with its NodeInfo.
///
/// The NodeInfo provides information about the concrete protocol spoken by the
/// node.
pub struct DescribedNode {
    pub node: fio::NodeSynchronousProxy,
    pub info: fio::NodeInfo,
}

/// Open the given path in the given directory.
///
/// The semantics for the flags and mode arguments are defined by the
/// fuchsia.io/Directory.Open message.
///
/// This function adds OPEN_FLAG_DESCRIBE to the given flags and then blocks
/// until the directory describes the newly opened node.
///
/// Returns the opened Node, along with its NodeInfo, or an error.
pub fn directory_open(
    directory: &fio::DirectorySynchronousProxy,
    path: &str,
    flags: u32,
    mode: u32,
    deadline: zx::Time,
) -> Result<DescribedNode, zx::Status> {
    let flags = flags | fio::OPEN_FLAG_DESCRIBE;

    let (client_end, server_end) = zx::Channel::create()?;
    directory.open(flags, mode, path, ServerEnd::new(server_end)).map_err(|_| zx::Status::IO)?;
    let node = fio::NodeSynchronousProxy::new(client_end);

    let fio::NodeEvent::OnOpen_ { s: status, info } =
        node.wait_for_event(deadline).map_err(|_| zx::Status::IO)?;

    zx::Status::ok(status)?;
    Ok(DescribedNode { node, info: *info.ok_or(zx::Status::IO)? })
}

/// Open a VMO at the given path in the given directory.
///
/// The semantics for the vmo_flags argument are defined by the
/// fuchsia.io/File.GetBuffer message (i.e., VMO_FLAG_*).
///
/// If the node at the given path is not a VMO, then this function returns
/// a zx::Status::IO error.
pub fn directory_open_vmo(
    directory: &fio::DirectorySynchronousProxy,
    path: &str,
    vmo_flags: u32,
    deadline: zx::Time,
) -> Result<zx::Vmo, zx::Status> {
    let mut open_flags = 0;
    if (vmo_flags & fio::VMO_FLAG_WRITE) != 0 {
        open_flags |= fio::OPEN_RIGHT_WRITABLE;
    }
    if (vmo_flags & fio::VMO_FLAG_READ) != 0 {
        open_flags |= fio::OPEN_RIGHT_READABLE;
    }
    if (vmo_flags & fio::VMO_FLAG_EXEC) != 0 {
        open_flags |= fio::OPEN_RIGHT_EXECUTABLE;
    }

    let description = directory_open(directory, path, open_flags, 0, deadline)?;
    let file = match description.info {
        fio::NodeInfo::File(_) => fio::FileSynchronousProxy::new(description.node.into_channel()),
        _ => return Err(zx::Status::IO),
    };

    let (status, buffer) = file.get_buffer(vmo_flags, deadline).map_err(|_| zx::Status::IO)?;
    zx::Status::ok(status)?;
    Ok(buffer.ok_or(zx::Status::IO)?.vmo)
}

/// Open the given path in the given directory without blocking.
///
/// A zx::Channel to the opened node is returned (or an error).
///
/// It is an error to supply the OPEN_FLAG_DESCRIBE flag in flags.
///
/// This function will "succeed" even if the given path does not exist in the
/// given directory because this function does not wait for the directory to
/// confirm that the path exists.
pub fn directory_open_async(
    directory: &fio::DirectorySynchronousProxy,
    path: &str,
    flags: u32,
    mode: u32,
) -> Result<zx::Channel, zx::Status> {
    if (flags & fio::OPEN_FLAG_DESCRIBE) != 0 {
        return Err(zx::Status::INVALID_ARGS);
    }

    let (client_end, server_end) = zx::Channel::create()?;
    directory.open(flags, mode, path, ServerEnd::new(server_end)).map_err(|_| zx::Status::IO)?;
    Ok(client_end)
}

/// Open a directory at the given path in the given directory without blocking.
///
/// This function adds the OPEN_FLAG_DIRECTORY flag and uses the
/// MODE_TYPE_DIRECTORY mode to ensure that the open operation completes only
/// if the given path is actually a directory, which means clients can start
/// using the returned DirectorySynchronousProxy immediately without waiting
/// for the server to complete the operation.
///
/// This function will "succeed" even if the given path does not exist in the
/// given directory or if the path is not a directory because this function
/// does not wait for the directory to confirm that the path exists and is a
/// directory.
pub fn directory_open_directory_async(
    directory: &fio::DirectorySynchronousProxy,
    path: &str,
    flags: u32,
) -> Result<fio::DirectorySynchronousProxy, zx::Status> {
    let flags = flags | fio::OPEN_FLAG_DIRECTORY;
    let mode = fio::MODE_TYPE_DIRECTORY;
    let client = directory_open_async(directory, path, flags, mode)?;
    Ok(fio::DirectorySynchronousProxy::new(client))
}

pub fn directory_clone(
    directory: &fio::DirectorySynchronousProxy,
    flags: u32,
) -> Result<fio::DirectorySynchronousProxy, zx::Status> {
    let (client_end, server_end) = zx::Channel::create()?;
    directory.clone(flags, ServerEnd::new(server_end)).map_err(|_| zx::Status::IO)?;
    Ok(fio::DirectorySynchronousProxy::new(client_end))
}

pub fn file_clone(
    file: &fio::FileSynchronousProxy,
    flags: u32,
) -> Result<fio::FileSynchronousProxy, zx::Status> {
    let (client_end, server_end) = zx::Channel::create()?;
    file.clone(flags, ServerEnd::new(server_end)).map_err(|_| zx::Status::IO)?;
    Ok(fio::FileSynchronousProxy::new(client_end))
}

#[cfg(test)]
mod test {
    use super::*;

    use anyhow::Error;
    use fidl::endpoints::Proxy;
    use fidl_fuchsia_io as fio;
    use fuchsia_async as fasync;
    use fuchsia_zircon::{AsHandleRef, HandleBased};
    use io_util::directory;

    fn open_pkg() -> fio::DirectorySynchronousProxy {
        let pkg_proxy = directory::open_in_namespace(
            "/pkg",
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        )
        .expect("failed to open /pkg");
        fio::DirectorySynchronousProxy::new(
            pkg_proxy
                .into_channel()
                .expect("failed to convert proxy into channel")
                .into_zx_channel(),
        )
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_directory_open() -> Result<(), Error> {
        let pkg = open_pkg();
        let description = directory_open(
            &pkg,
            "bin/syncio_lib_test",
            fio::OPEN_RIGHT_READABLE,
            0,
            zx::Time::INFINITE,
        )?;
        assert!(match description.info {
            fio::NodeInfo::File(_) => true,
            _ => false,
        });
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_directory_open_vmo() -> Result<(), Error> {
        let pkg = open_pkg();
        let vmo = directory_open_vmo(
            &pkg,
            "bin/syncio_lib_test",
            fio::VMO_FLAG_READ | fio::VMO_FLAG_EXEC,
            zx::Time::INFINITE,
        )?;
        assert!(!vmo.is_invalid_handle());

        let info = vmo.basic_info()?;
        assert_eq!(zx::Rights::READ, info.rights & zx::Rights::READ);
        assert_eq!(zx::Rights::EXECUTE, info.rights & zx::Rights::EXECUTE);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_directory_open_directory_async() -> Result<(), Error> {
        let pkg = open_pkg();
        let bin = directory_open_directory_async(
            &pkg,
            "bin",
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        )?;
        let vmo = directory_open_vmo(
            &bin,
            "syncio_lib_test",
            fio::VMO_FLAG_READ | fio::VMO_FLAG_EXEC,
            zx::Time::INFINITE,
        )?;
        assert!(!vmo.is_invalid_handle());

        let info = vmo.basic_info()?;
        assert_eq!(zx::Rights::READ, info.rights & zx::Rights::READ);
        assert_eq!(zx::Rights::EXECUTE, info.rights & zx::Rights::EXECUTE);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_directory_open_zxio_async() -> Result<(), Error> {
        let pkg_proxy = directory::open_in_namespace(
            "/pkg",
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        )
        .expect("failed to open /pkg");
        let zx_channel = pkg_proxy
            .into_channel()
            .expect("failed to convert proxy into channel")
            .into_zx_channel();
        let storage = zxio::zxio_storage_t::default();
        let status = unsafe {
            zxio::zxio_create(
                zx_channel.into_raw(),
                &storage as *const zxio::zxio_storage_t as *mut zxio::zxio_storage_t,
            )
        };
        assert_eq!(status, zx::sys::ZX_OK);
        let io = &storage.io as *const zxio::zxio_t as *mut zxio::zxio_t;
        let close_status = unsafe { zxio::zxio_close(io) };
        assert_eq!(close_status, zx::sys::ZX_OK);
        Ok(())
    }
}
