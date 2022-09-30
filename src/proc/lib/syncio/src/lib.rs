// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::From;

use crate::zxio::{zxio_dirent_iterator_next, zxio_dirent_iterator_t};
use bitflags::bitflags;
use fidl::encoding::const_assert_eq;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_io as fio;
use fuchsia_zircon::{self as zx, HandleBased};
use linux_uapi::x86_64::__kernel_sockaddr_storage as sockaddr_storage;
use zxio::{sockaddr, socklen_t, ZXIO_SHUTDOWN_OPTIONS_READ, ZXIO_SHUTDOWN_OPTIONS_WRITE};

pub mod zxio;

pub use zxio::zxio_dirent_t;
pub use zxio::zxio_node_attributes_t;
pub use zxio::zxio_signals_t;

bitflags! {
    // These values should match the values in sdk/lib/zxio/include/lib/zxio/types.h
    pub struct ZxioSignals : zxio_signals_t {
        const NONE            =      0;
        const READABLE        = 1 << 0;
        const WRITABLE        = 1 << 1;
        const READ_DISABLED   = 1 << 2;
        const WRITE_DISABLED  = 1 << 3;
        const READ_THRESHOLD  = 1 << 4;
        const WRITE_THRESHOLD = 1 << 5;
        const OUT_OF_BAND     = 1 << 6;
        const ERROR           = 1 << 7;
        const PEER_CLOSED     = 1 << 8;
    }
}

bitflags! {
    /// The flags for shutting down sockets.
    pub struct ZxioShutdownFlags: u32 {
        /// Further transmissions will be disallowed.
        const WRITE = 1 << 0;

        /// Further receptions will be disallowed.
        const READ = 1 << 1;
    }
}

const_assert_eq!(ZxioShutdownFlags::WRITE.bits(), ZXIO_SHUTDOWN_OPTIONS_WRITE);
const_assert_eq!(ZxioShutdownFlags::READ.bits(), ZXIO_SHUTDOWN_OPTIONS_READ);

// TODO: We need a more comprehensive error strategy.
// Our dependencies create elaborate error objects, but Starnix would prefer
// this library produce zx::Status errors for easier conversion to Errno.

#[derive(Default, Debug)]
pub struct ZxioDirent {
    pub protocols: Option<zxio::zxio_node_protocols_t>,
    pub abilities: Option<zxio::zxio_abilities_t>,
    pub id: Option<zxio::zxio_id_t>,
    pub name: Vec<u8>,
}

pub struct DirentIterator {
    iterator: Box<zxio_dirent_iterator_t>,

    /// Whether the iterator has reached the end of dir entries.
    /// This is necessary because the zxio API returns only once the error code
    /// indicating the iterator has reached the end, where subsequent calls may
    /// return other error codes.
    finished: bool,
}

/// It is important that all methods here are &mut self, to require the client
/// to obtain exclusive access to the object, externally locking it.
impl Iterator for DirentIterator {
    type Item = Result<ZxioDirent, zx::Status>;

    /// Returns the next dir entry for this iterator.
    fn next(&mut self) -> Option<Result<ZxioDirent, zx::Status>> {
        if self.finished {
            return None;
        }
        let mut entry = zxio_dirent_t::default();
        let mut name_buffer = Vec::with_capacity(fio::MAX_FILENAME as usize);
        // The FFI interface expects a pointer to std::os::raw:c_char which is i8 on Fuchsia.
        // The Rust str and OsStr types expect raw character data to be stored in a buffer u8 values.
        // The types are equivalent for all practical purposes and Rust permits casting between the types,
        // so we insert a type cast here in the FFI bindings.
        entry.name = name_buffer.as_mut_ptr() as *mut std::os::raw::c_char;
        let status = unsafe { zxio_dirent_iterator_next(&mut *self.iterator.as_mut(), &mut entry) };
        let result = match zx::ok(status) {
            Ok(()) => {
                let result = ZxioDirent::from(entry, name_buffer);
                Ok(result)
            }
            Err(zx::Status::NOT_FOUND) => {
                self.finished = true;
                return None;
            }
            Err(e) => Err(e),
        };
        return Some(result);
    }
}

impl Drop for DirentIterator {
    fn drop(&mut self) {
        unsafe {
            zxio::zxio_dirent_iterator_destroy(&mut *self.iterator.as_mut());
        };
    }
}

unsafe impl Send for DirentIterator {}
unsafe impl Sync for DirentIterator {}

impl ZxioDirent {
    fn from(dirent: zxio_dirent_t, name_buffer: Vec<u8>) -> ZxioDirent {
        let protocols = if dirent.has.protocols { Some(dirent.protocols) } else { None };
        let abilities = if dirent.has.abilities { Some(dirent.abilities) } else { None };
        let id = if dirent.has.id { Some(dirent.id) } else { None };
        let mut name = name_buffer;
        unsafe { name.set_len(dirent.name_length as usize) };
        ZxioDirent { protocols, abilities, id, name }
    }
}

pub struct ZxioErrorCode(i16);

#[derive(Default)]
pub struct Zxio {
    storage: zxio::zxio_storage_t,
}

impl Zxio {
    fn as_ptr(&self) -> *mut zxio::zxio_t {
        &self.storage.io as *const zxio::zxio_t as *mut zxio::zxio_t
    }

    pub fn as_storage_ptr(&self) -> *mut zxio::zxio_storage_t {
        &self.storage as *const zxio::zxio_storage_t as *mut zxio::zxio_storage_t
    }

    pub fn create(handle: zx::Handle) -> Result<Zxio, zx::Status> {
        let zxio = Zxio::default();
        let status = unsafe { zxio::zxio_create(handle.into_raw(), zxio.as_storage_ptr()) };
        zx::ok(status)?;
        Ok(zxio)
    }

    pub fn open(&self, flags: fio::OpenFlags, mode: u32, path: &str) -> Result<Zxio, zx::Status> {
        let zxio = Zxio::default();
        let status = unsafe {
            zxio::zxio_open(
                self.as_ptr(),
                flags.bits(),
                mode,
                path.as_ptr() as *const ::std::os::raw::c_char,
                path.len(),
                zxio.as_storage_ptr(),
            )
        };
        zx::ok(status)?;
        Ok(zxio)
    }

    pub fn read(&self, data: &mut [u8]) -> Result<usize, zx::Status> {
        let flags = zxio::zxio_flags_t::default();
        let mut actual = 0usize;
        let status = unsafe {
            zxio::zxio_read(
                self.as_ptr(),
                data.as_ptr() as *mut ::std::os::raw::c_void,
                data.len(),
                flags,
                &mut actual,
            )
        };
        zx::ok(status)?;
        Ok(actual)
    }

    pub fn clone(&self) -> Result<Zxio, zx::Status> {
        let mut handle = 0;
        let status = unsafe { zxio::zxio_clone(self.as_ptr(), &mut handle) };
        zx::ok(status)?;
        unsafe { Zxio::create(zx::Handle::from_raw(handle)) }
    }

    pub fn read_at(&self, offset: u64, data: &mut [u8]) -> Result<usize, zx::Status> {
        let flags = zxio::zxio_flags_t::default();
        let mut actual = 0usize;
        let status = unsafe {
            zxio::zxio_read_at(
                self.as_ptr(),
                offset,
                data.as_ptr() as *mut ::std::os::raw::c_void,
                data.len(),
                flags,
                &mut actual,
            )
        };
        zx::ok(status)?;
        Ok(actual)
    }

    pub fn write(&self, data: &[u8]) -> Result<usize, zx::Status> {
        let flags = zxio::zxio_flags_t::default();
        let mut actual = 0;
        let status = unsafe {
            zxio::zxio_write(
                self.as_ptr(),
                data.as_ptr() as *const ::std::os::raw::c_void,
                data.len(),
                flags,
                &mut actual,
            )
        };
        zx::ok(status)?;
        Ok(actual)
    }

    pub fn write_at(&self, offset: u64, data: &[u8]) -> Result<usize, zx::Status> {
        let flags = zxio::zxio_flags_t::default();
        let mut actual = 0;
        let status = unsafe {
            zxio::zxio_write_at(
                self.as_ptr(),
                offset,
                data.as_ptr() as *const ::std::os::raw::c_void,
                data.len(),
                flags,
                &mut actual,
            )
        };
        zx::ok(status)?;
        Ok(actual)
    }

    pub fn truncate(&self, length: u64) -> Result<(), zx::Status> {
        let status = unsafe { zxio::zxio_truncate(self.as_ptr(), length) };
        zx::ok(status)?;
        Ok(())
    }

    pub fn vmo_get(&self, flags: zx::VmarFlags) -> Result<zx::Vmo, zx::Status> {
        let mut vmo = 0;
        let status = unsafe { zxio::zxio_vmo_get(self.as_ptr(), flags.bits(), &mut vmo) };
        zx::ok(status)?;
        let handle = unsafe { zx::Handle::from_raw(vmo) };
        Ok(zx::Vmo::from(handle))
    }

    pub fn attr_get(&self) -> Result<zxio_node_attributes_t, zx::Status> {
        let mut attributes = zxio_node_attributes_t::default();
        let status = unsafe { zxio::zxio_attr_get(self.as_ptr(), &mut attributes) };
        zx::ok(status)?;
        Ok(attributes)
    }

    pub fn wait_begin(
        &self,
        zxio_signals: zxio_signals_t,
    ) -> (zx::Unowned<'_, zx::Handle>, zx::Signals) {
        let mut handle = zx::sys::ZX_HANDLE_INVALID;
        let mut zx_signals = zx::sys::ZX_SIGNAL_NONE;
        unsafe { zxio::zxio_wait_begin(self.as_ptr(), zxio_signals, &mut handle, &mut zx_signals) };
        let handle = unsafe { zx::Unowned::<zx::Handle>::from_raw_handle(handle) };
        let signals = zx::Signals::from_bits_truncate(zx_signals);
        (handle, signals)
    }

    pub fn wait_end(&self, signals: zx::Signals) -> zxio_signals_t {
        let mut zxio_signals = ZxioSignals::NONE.bits();
        unsafe {
            zxio::zxio_wait_end(self.as_ptr(), signals.bits(), &mut zxio_signals);
        }
        zxio_signals
    }

    pub fn create_dirent_iterator(&self) -> Result<DirentIterator, zx::Status> {
        let mut zxio_iterator = Box::default();
        let status = unsafe { zxio::zxio_dirent_iterator_init(&mut *zxio_iterator, self.as_ptr()) };
        let iterator = DirentIterator { iterator: zxio_iterator, finished: false };
        zx::ok(status)?;
        Ok(iterator)
    }

    pub fn connect(&self, addr: &[u8]) -> Result<ZxioErrorCode, zx::Status> {
        let mut out_code = 0;
        let status = unsafe {
            zxio::zxio_connect(
                self.as_ptr(),
                addr.as_ptr() as *const sockaddr,
                addr.len() as socklen_t,
                &mut out_code,
            )
        };
        zx::ok(status)?;
        Ok(ZxioErrorCode(out_code))
    }

    pub fn bind(&self, addr: &[u8]) -> Result<ZxioErrorCode, zx::Status> {
        let mut out_code = 0;
        let status = unsafe {
            zxio::zxio_bind(
                self.as_ptr(),
                addr.as_ptr() as *const sockaddr,
                addr.len() as socklen_t,
                &mut out_code,
            )
        };
        zx::ok(status)?;
        Ok(ZxioErrorCode(out_code))
    }

    pub fn listen(&self, backlog: i32) -> Result<ZxioErrorCode, zx::Status> {
        let mut out_code = 0;
        let status = unsafe {
            zxio::zxio_listen(self.as_ptr(), backlog as std::os::raw::c_int, &mut out_code)
        };
        zx::ok(status)?;
        Ok(ZxioErrorCode(out_code))
    }

    pub fn accept(&self) -> Result<(Zxio, ZxioErrorCode), zx::Status> {
        let mut addrlen = std::mem::size_of::<sockaddr_storage>() as socklen_t;
        let mut addr = vec![0u8; addrlen as usize];
        let zxio = Zxio::default();
        let mut out_code = 0;
        let status = unsafe {
            zxio::zxio_accept(
                self.as_ptr(),
                addr.as_mut_ptr() as *mut sockaddr,
                &mut addrlen,
                zxio.as_storage_ptr(),
                &mut out_code,
            )
        };
        zx::ok(status)?;
        Ok((zxio, ZxioErrorCode(out_code)))
    }

    pub fn getsockname(&self) -> Result<(Vec<u8>, ZxioErrorCode), zx::Status> {
        let mut addrlen = std::mem::size_of::<sockaddr_storage>() as socklen_t;
        let mut addr = vec![0u8; addrlen as usize];
        let mut out_code = 0;
        let status = unsafe {
            zxio::zxio_getsockname(
                self.as_ptr(),
                addr.as_mut_ptr() as *mut sockaddr,
                &mut addrlen,
                &mut out_code,
            )
        };
        zx::ok(status)?;
        Ok((addr[..addrlen as usize].to_vec(), ZxioErrorCode(out_code)))
    }

    pub fn getpeername(&self) -> Result<(Vec<u8>, ZxioErrorCode), zx::Status> {
        let mut addrlen = std::mem::size_of::<sockaddr_storage>() as socklen_t;
        let mut addr = vec![0u8; addrlen as usize];
        let mut out_code = 0;
        let status = unsafe {
            zxio::zxio_getpeername(
                self.as_ptr(),
                addr.as_mut_ptr() as *mut sockaddr,
                &mut addrlen,
                &mut out_code,
            )
        };
        zx::ok(status)?;
        Ok((addr[..addrlen as usize].to_vec(), ZxioErrorCode(out_code)))
    }

    pub fn getsockopt(
        &self,
        level: u32,
        optname: u32,
        mut optlen: socklen_t,
    ) -> Result<(Vec<u8>, ZxioErrorCode), zx::Status> {
        let mut optval = vec![0u8; optlen as usize];
        let mut out_code = 0;
        let status = unsafe {
            zxio::zxio_getsockopt(
                self.as_ptr(),
                level as std::os::raw::c_int,
                optname as std::os::raw::c_int,
                optval.as_mut_ptr() as *mut std::os::raw::c_void,
                &mut optlen,
                &mut out_code,
            )
        };
        zx::ok(status)?;
        Ok((optval[..optlen as usize].to_vec(), ZxioErrorCode(out_code)))
    }

    pub fn setsockopt(
        &self,
        level: i32,
        optname: i32,
        optval: &[u8],
    ) -> Result<ZxioErrorCode, zx::Status> {
        let mut out_code = 0;
        let status = unsafe {
            zxio::zxio_setsockopt(
                self.as_ptr(),
                level,
                optname,
                optval.as_ptr() as *const std::os::raw::c_void,
                optval.len() as socklen_t,
                &mut out_code,
            )
        };
        zx::ok(status)?;
        Ok(ZxioErrorCode(out_code))
    }

    pub fn shutdown(&self, flags: ZxioShutdownFlags) -> Result<ZxioErrorCode, zx::Status> {
        let mut out_code = 0;
        let status = unsafe { zxio::zxio_shutdown(self.as_ptr(), flags.bits(), &mut out_code) };
        zx::ok(status)?;
        Ok(ZxioErrorCode(out_code))
    }
}

impl Drop for Zxio {
    fn drop(&mut self) {
        unsafe {
            zxio::zxio_close(self.as_ptr());
        };
    }
}

enum NodeKind {
    File,
    Directory,
    Unknown,
}

impl NodeKind {
    fn from(info: &fio::NodeInfoDeprecated) -> NodeKind {
        match info {
            fio::NodeInfoDeprecated::File(_) => NodeKind::File,
            fio::NodeInfoDeprecated::Directory(_) => NodeKind::Directory,
            _ => NodeKind::Unknown,
        }
    }

    fn from2(representation: &fio::Representation) -> NodeKind {
        match representation {
            fio::Representation::File(_) => NodeKind::File,
            fio::Representation::Directory(_) => NodeKind::Directory,
            _ => NodeKind::Unknown,
        }
    }
}

/// A fuchsia.io.Node along with its NodeInfoDeprecated.
///
/// The NodeInfoDeprecated provides information about the concrete protocol spoken by the
/// node.
struct DescribedNode {
    node: fio::NodeSynchronousProxy,
    kind: NodeKind,
}

/// Open the given path in the given directory.
///
/// The semantics for the flags and mode arguments are defined by the
/// fuchsia.io/Directory.Open message.
///
/// This function adds OPEN_FLAG_DESCRIBE to the given flags and then blocks
/// until the directory describes the newly opened node.
///
/// Returns the opened Node, along with its NodeInfoDeprecated, or an error.
fn directory_open(
    directory: &fio::DirectorySynchronousProxy,
    path: &str,
    flags: fio::OpenFlags,
    mode: u32,
    deadline: zx::Time,
) -> Result<DescribedNode, zx::Status> {
    let flags = flags | fio::OpenFlags::DESCRIBE;

    let (client_end, server_end) = zx::Channel::create()?;
    directory.open(flags, mode, path, ServerEnd::new(server_end)).map_err(|_| zx::Status::IO)?;
    let node = fio::NodeSynchronousProxy::new(client_end);

    match node.wait_for_event(deadline).map_err(|_| zx::Status::IO)? {
        fio::NodeEvent::OnOpen_ { s: status, info } => {
            zx::Status::ok(status)?;
            Ok(DescribedNode { node, kind: NodeKind::from(&*info.ok_or(zx::Status::IO)?) })
        }
        fio::NodeEvent::OnRepresentation { payload } => {
            Ok(DescribedNode { node, kind: NodeKind::from2(&payload) })
        }
    }
}

/// Open a VMO at the given path in the given directory.
///
/// The semantics for the vmo_flags argument are defined by the
/// fuchsia.io/File.GetBackingMemory message (i.e., VmoFlags::*).
///
/// If the node at the given path is not a VMO, then this function returns
/// a zx::Status::IO error.
pub fn directory_open_vmo(
    directory: &fio::DirectorySynchronousProxy,
    path: &str,
    vmo_flags: fio::VmoFlags,
    deadline: zx::Time,
) -> Result<zx::Vmo, zx::Status> {
    let mut open_flags = fio::OpenFlags::empty();
    if vmo_flags.contains(fio::VmoFlags::WRITE) {
        open_flags |= fio::OpenFlags::RIGHT_WRITABLE;
    }
    if vmo_flags.contains(fio::VmoFlags::READ) {
        open_flags |= fio::OpenFlags::RIGHT_READABLE;
    }
    if vmo_flags.contains(fio::VmoFlags::EXECUTE) {
        open_flags |= fio::OpenFlags::RIGHT_EXECUTABLE;
    }

    let description = directory_open(directory, path, open_flags, 0, deadline)?;
    let file = match description.kind {
        NodeKind::File => fio::FileSynchronousProxy::new(description.node.into_channel()),
        _ => return Err(zx::Status::IO),
    };

    let vmo = file
        .get_backing_memory(vmo_flags, deadline)
        .map_err(|_: fidl::Error| zx::Status::IO)?
        .map_err(zx::Status::from_raw)?;
    Ok(vmo)
}

/// Read the content of the file at the given path in the given directory.
///
/// If the node at the given path is not a file, then this function returns
/// a zx::Status::IO error.
pub fn directory_read_file(
    directory: &fio::DirectorySynchronousProxy,
    path: &str,
    deadline: zx::Time,
) -> Result<Vec<u8>, zx::Status> {
    let description = directory_open(directory, path, fio::OpenFlags::RIGHT_READABLE, 0, deadline)?;
    let file = match description.kind {
        NodeKind::File => fio::FileSynchronousProxy::new(description.node.into_channel()),
        _ => return Err(zx::Status::IO),
    };

    let mut result = Vec::new();
    loop {
        let mut data = file
            .read(fio::MAX_TRANSFER_SIZE, deadline)
            .map_err(|_: fidl::Error| zx::Status::IO)?
            .map_err(zx::Status::from_raw)?;
        let finished = (data.len() as u64) < fio::MAX_TRANSFER_SIZE;
        result.append(&mut data);
        if finished {
            return Ok(result);
        }
    }
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
    flags: fio::OpenFlags,
    mode: u32,
) -> Result<zx::Channel, zx::Status> {
    if flags.intersects(fio::OpenFlags::DESCRIBE) {
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
    flags: fio::OpenFlags,
) -> Result<fio::DirectorySynchronousProxy, zx::Status> {
    let flags = flags | fio::OpenFlags::DIRECTORY;
    let mode = fio::MODE_TYPE_DIRECTORY;
    let client = directory_open_async(directory, path, flags, mode)?;
    Ok(fio::DirectorySynchronousProxy::new(client))
}

pub fn directory_clone(
    directory: &fio::DirectorySynchronousProxy,
    flags: fio::OpenFlags,
) -> Result<fio::DirectorySynchronousProxy, zx::Status> {
    let (client_end, server_end) = zx::Channel::create()?;
    directory.clone(flags, ServerEnd::new(server_end)).map_err(|_| zx::Status::IO)?;
    Ok(fio::DirectorySynchronousProxy::new(client_end))
}

pub fn file_clone(
    file: &fio::FileSynchronousProxy,
    flags: fio::OpenFlags,
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
    use fuchsia_fs::directory;
    use fuchsia_zircon::{AsHandleRef, HandleBased};

    fn open_pkg() -> fio::DirectorySynchronousProxy {
        let pkg_proxy = directory::open_in_namespace(
            "/pkg",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
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
            fio::OpenFlags::RIGHT_READABLE,
            0,
            zx::Time::INFINITE,
        )?;
        assert!(match description.kind {
            NodeKind::File => true,
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
            fio::VmoFlags::READ | fio::VmoFlags::EXECUTE,
            zx::Time::INFINITE,
        )?;
        assert!(!vmo.is_invalid_handle());

        let info = vmo.basic_info()?;
        assert_eq!(zx::Rights::READ, info.rights & zx::Rights::READ);
        assert_eq!(zx::Rights::EXECUTE, info.rights & zx::Rights::EXECUTE);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_directory_read_file() -> Result<(), Error> {
        let pkg = open_pkg();
        let data = directory_read_file(&pkg, "bin/syncio_lib_test", zx::Time::INFINITE)?;

        assert!(!data.is_empty());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_directory_open_directory_async() -> Result<(), Error> {
        let pkg = open_pkg();
        let bin = directory_open_directory_async(
            &pkg,
            "bin",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        )?;
        let vmo = directory_open_vmo(
            &bin,
            "syncio_lib_test",
            fio::VmoFlags::READ | fio::VmoFlags::EXECUTE,
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
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
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

    #[fuchsia::test]
    async fn test_directory_enumerate() -> Result<(), Error> {
        let pkg_dir_handle = directory::open_in_namespace(
            "/pkg",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        )
        .expect("failed to open /pkg")
        .into_channel()
        .expect("could not unwrap channel")
        .into_zx_channel()
        .into();

        let io: Zxio = Zxio::create(pkg_dir_handle)?;
        let iter = io.create_dirent_iterator().expect("failed to create iterator");
        let expected_dir_names = vec![".", "bin", "lib", "meta"];
        let mut found_dir_names = iter
            .map(|e| {
                std::str::from_utf8(&e.unwrap().name).expect("name was not valid utf8").to_string()
            })
            .collect::<Vec<_>>();
        found_dir_names.sort();
        assert_eq!(expected_dir_names, found_dir_names);
        Ok(())
    }
}
