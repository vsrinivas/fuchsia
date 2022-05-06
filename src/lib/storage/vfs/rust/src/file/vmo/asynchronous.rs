// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//!
//! Implementation of a file backed by a VMO buffer shared by all the file connections.  From the
//! library user side, these files are backed by asynchronous `init_vmo` callback.
//!
//! Connections to this kind of file synchronize and perform operations on a shared VMO initially
//! provided by the `init_vmo` callback. `init_vmo` callback is called when the first connection to
//! the file is established and is responsible for providing a VMO to be used by all future
//! connections.
//!

#![warn(missing_docs)]

pub mod test_utils;

#[cfg(test)]
mod tests;

use crate::{
    common::send_on_open_with_error,
    directory::entry::{DirectoryEntry, EntryInfo},
    execution_scope::ExecutionScope,
    file::vmo::connection::{io1::VmoFileConnection, AsyncInitVmo, VmoFileInterface},
    path::Path,
};

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio,
    fuchsia_zircon::{Status, Vmo, VmoOptions},
    futures::future::BoxFuture,
    futures::lock::{Mutex, MutexLockFuture},
    std::{future::Future, sync::Arc},
};

/// `init_vmo` callback returns an instance of this struct to describe the VMO it has generated, as
/// well as `size` and `capacity` restrictions for the new file.
pub struct NewVmo {
    /// `Vmo` object to be used for the file content.
    pub vmo: Vmo,

    /// VMOs area allocated in pages, while files are allocated in bytes.  There is no way to know
    /// what was the actual allocation size from the VMO alone, so the callback needs to tell the
    /// size explicitly.
    ///
    /// This number must be less than or equal than the allocated VMO size.  When this constraint
    /// is violated a debug build will panic, while a release build will try to resize the VMO to
    /// the specified size.  If the VMO is not resizable it will reduce the file size to match the
    /// allocation size.
    pub size: u64,

    /// Maximum size this `Vmo` can be extended to.  If this field is larger than the current size
    /// of the `Vmo`, then the `Vmo` will be resized if a request arrives that should touch data
    /// outside of the currently allocated size.  Make sure to provide a VMO with the
    /// `ZX_VMO_RESIZABLE` flag.
    ///
    /// If the `size` is larger than `capacity`, users of the file will be able to access bytes
    /// beyond the capacity - all `size` bytes of data.  But if the file is truncated, then the
    /// `capacity` will take over and will limit the access and resize operations.
    pub capacity: u64,
}

/// Connection buffer initialization result. It is either a byte buffer with the file content, or
/// an error code.
pub type InitVmoResult = Result<NewVmo, Status>;

/// Creates a new read-only `VmoFile` backed by the specified `init_vmo` handler.
///
/// The `init_vmo` handler is called to initialize a VMO for the very first connection to the file.
///
/// New connections are only allowed when they specify "read-only" access to the file content.
///
/// For more details on this interaction, see the module documentation.
pub fn read_only<InitVmo, InitVmoFuture>(init_vmo: InitVmo) -> Arc<VmoFile<InitVmo, InitVmoFuture>>
where
    InitVmo: Fn() -> InitVmoFuture + Send + Sync + 'static,
    InitVmoFuture: Future<Output = InitVmoResult> + Send + 'static,
{
    VmoFile::new(init_vmo, true, false, false)
}

/// Creates a new read-exec-only `VmoFile` backed by the specified `init_vmo` handler. It is the
/// responsibility of the caller to ensure that the handler provides a VMO with the correct rights
/// (namely, it must have both ZX_RIGHT_READ and ZX_RIGHT_EXECUTE).
///
/// The `init_vmo` handler is called to initialize a VMO for the very first connection to the file.
/// New connections are only allowed when they specify read and/or execute access to the file.
///
/// For more details on this interaction, see the module documentation.
pub fn read_exec<InitVmo, InitVmoFuture>(init_vmo: InitVmo) -> Arc<VmoFile<InitVmo, InitVmoFuture>>
where
    InitVmo: Fn() -> InitVmoFuture + Send + Sync + 'static,
    InitVmoFuture: Future<Output = InitVmoResult> + Send + 'static,
{
    VmoFile::new(init_vmo, true, false, true)
}

fn init_vmo<'a>(content: Arc<[u8]>) -> impl Fn() -> BoxFuture<'a, InitVmoResult> + Send + Sync {
    move || {
        // In "production" code we would instead wrap `content` in a smart pointer to be able to
        // share it with the async block, but for tests it is fine to just clone it.
        let content = content.clone();
        Box::pin(async move {
            let size = content.len() as u64;
            let vmo = Vmo::create(size)?;
            vmo.write(&content, 0)?;
            vmo.set_content_size(&size)?;
            Ok(NewVmo { vmo, size, capacity: size })
        })
    }
}

/// Creates a new read-only `VmoFile` which serves static content.  Also see
/// `read_only_const` which allows you to pass the ownership to the file itself.
pub fn read_only_static<Bytes>(
    bytes: Bytes,
) -> Arc<
    VmoFile<
        impl Fn() -> BoxFuture<'static, InitVmoResult> + Send + Sync + 'static,
        BoxFuture<'static, InitVmoResult>,
    >,
>
where
    Bytes: AsRef<[u8]> + Send + Sync,
{
    let content: Arc<[u8]> = bytes.as_ref().to_vec().clone().into();
    read_only(init_vmo(content.clone()))
}

/// Create a new read-only `VmoFile` which servers a constant content.  The difference with
/// `read_only_static` is that this function takes a run time values that it will own, while
/// `read_only_static` requires a reference to something with a static lifetime.
pub fn read_only_const(
    bytes: &[u8],
) -> Arc<
    VmoFile<
        impl Fn() -> BoxFuture<'static, InitVmoResult> + Send + Sync,
        BoxFuture<'static, InitVmoResult>,
    >,
> {
    let content: Arc<[u8]> = bytes.to_vec().clone().into();
    read_only(init_vmo(content.clone()))
}

/// Just like `simple_init_vmo`, but allows one to specify the capacity explicitly, instead of
/// setting it to be the max of 100 and the content size.  The VMO is sized to be the
/// maximum of the `content` length and the specified `capacity`.
pub fn simple_init_vmo_with_capacity(
    content: &[u8],
    capacity: u64,
) -> impl Fn() -> BoxFuture<'static, InitVmoResult> + Send + Sync + 'static {
    let content = content.to_vec();
    move || {
        // In "production" code we would instead wrap `content` in a smart pointer to be able to
        // share it with the async block, but for tests it is fine to just clone it.
        let content = content.clone();
        Box::pin(async move {
            let size = content.len() as u64;
            let vmo_size = std::cmp::max(size, capacity);
            let vmo = Vmo::create(vmo_size)?;
            if content.len() > 0 {
                vmo.write(&content, 0)?;
            }
            vmo.set_content_size(&size)?;
            Ok(NewVmo { vmo, size, capacity })
        })
    }
}

/// Similar to the `simple_init_vmo`, but the produced VMOs are resizable, and the `capacity` is
/// set by the caller.  Note that this capacity is a limitation enforced by the FIDL binding layer,
/// not something applied to the VMO.  The VMO is initially sized to be the maximum of the
/// `content` length and the specified `capacity`.
pub fn simple_init_vmo_resizable_with_capacity(
    content: &[u8],
    capacity: u64,
) -> impl Fn() -> BoxFuture<'static, InitVmoResult> + Send + Sync + 'static {
    let content = content.to_vec();
    move || {
        let content = content.clone();
        Box::pin(async move {
            let vmo_size = content.len() as u64;
            let vmo = Vmo::create_with_opts(VmoOptions::RESIZABLE, vmo_size)?;
            if vmo_size > 0 {
                vmo.write(&content, 0)?;
            }
            vmo.set_content_size(&vmo_size)?;
            Ok(NewVmo { vmo, size: vmo_size, capacity })
        })
    }
}

/// Creates new `VmoFile` backed by the specified `init_vmo` handler.
///
/// The `init_vmo` handler is called to initialize a VMO for the very first connection to the file.
///
/// New connections may specify any kind of access to the file content.
///
/// For more details on these interaction, see the module documentation.
pub fn read_write<InitVmo, InitVmoFuture>(init_vmo: InitVmo) -> Arc<VmoFile<InitVmo, InitVmoFuture>>
where
    InitVmo: Fn() -> InitVmoFuture + Send + Sync + 'static,
    InitVmoFuture: Future<Output = InitVmoResult> + Send + 'static,
{
    VmoFile::new(init_vmo, true, true, false)
}

/// Implementation of an asynchronous VMO-backed file in a virtual file system. This is created by
/// passing async `init_vmo` callback to the exported constructor functions.
///
/// Futures returned by these callbacks will be executed by the library using connection specific
/// [`ExecutionScope`].
///
/// See the module documentation for more details.
pub struct VmoFile<InitVmo, InitVmoFuture>
where
    InitVmo: Fn() -> InitVmoFuture + Send + Sync + 'static,
    InitVmoFuture: Future<Output = InitVmoResult> + Send + 'static,
{
    init_vmo: InitVmo,
    /// Specifies if the file is readable. `init_vmo` is always invoked even for non-readable VMOs.
    readable: bool,

    /// Specifies if the file is writable.
    writable: bool,

    /// Specifies if the file can be opened as executable.
    executable: bool,

    /// Specifies the inode for this file. If you don't care or don't know, INO_UNKNOWN can be
    /// used.
    inode: u64,

    // File connections share state with the file itself.
    // TODO: It should be `pub(in super::connection)` but the compiler claims, `super` does not
    // contain a `connection`.  Neither `pub(in create:vmo::connection)` works.
    pub(super) state: Mutex<VmoFileState>,
}

/// State shared between all the connections to a file, across all execution scopes.
// TODO: It should be `pub(in super::connection)` but the compiler claims, `super` does not contain
// a `connection`.  Neither the `pub(in create:vmo::connection)` works.
pub(super) enum VmoFileState {
    /// No connections currently exist for this file.
    Uninitialized,

    /// File currently has one or more connections.
    Initialized {
        vmo: Vmo,

        /// Size of the file, as observed via the `File` FIDL protocol.  Must be less than or equal
        /// to the `vmo_size`.
        size: u64,

        /// Size of the `vmo` - to save on a system call.  Must be no less than `size`.
        vmo_size: u64,

        /// Maximum capacity we are allowed to extend the VMO size to.
        capacity: u64,

        /// Number of active connections to the file.
        connection_count: u64,
    },
}

impl<InitVmo, InitVmoFuture> VmoFile<InitVmo, InitVmoFuture>
where
    InitVmo: Fn() -> InitVmoFuture + Send + Sync + 'static,
    InitVmoFuture: Future<Output = InitVmoResult> + Send + 'static,
{
    /// Create a new VmoFile. The reported inode value will be [`fio::INO_UNKNOWN`].
    /// See [`VmoFile::new_with_inode()`] to construct a VmoFile with an explicit inode value.
    ///
    /// # Arguments
    ///
    /// * `init_vmo` - Async callback to create the Vmo backing this file upon first connection.
    /// * `readable` - If true, allow connections with OpenFlags::RIGHT_READABLE.
    /// * `writable` - If true, allow connections with OpenFlags::RIGHT_WRITABLE.
    /// * `executable` - If true, allow connections with OpenFlags::RIGHT_EXECUTABLE.
    pub fn new(init_vmo: InitVmo, readable: bool, writable: bool, executable: bool) -> Arc<Self> {
        Self::new_with_inode(init_vmo, readable, writable, executable, fio::INO_UNKNOWN)
    }

    /// Create a new VmoFile with the specified options and inode value.
    ///
    /// # Arguments
    ///
    /// * `init_vmo` - Async callback to create the Vmo backing this file upon first connection.
    /// * `readable` - If true, allow connections with OpenFlags::RIGHT_READABLE.
    /// * `writable` - If true, allow connections with OpenFlags::RIGHT_WRITABLE.
    /// * `executable` - If true, allow connections with OpenFlags::RIGHT_EXECUTABLE.
    /// * `inode` - Inode value to report when getting the VmoFile's attributes.
    pub fn new_with_inode(
        init_vmo: InitVmo,
        readable: bool,
        writable: bool,
        executable: bool,
        inode: u64,
    ) -> Arc<Self> {
        Arc::new(VmoFile {
            init_vmo,
            readable,
            writable,
            executable,
            inode,
            state: Mutex::new(VmoFileState::Uninitialized),
        })
    }
}

impl<InitVmo, InitVmoFuture> VmoFileInterface for VmoFile<InitVmo, InitVmoFuture>
where
    InitVmo: Fn() -> InitVmoFuture + Send + Sync + 'static,
    InitVmoFuture: Future<Output = InitVmoResult> + Send + 'static,
{
    fn init_vmo(self: Arc<Self>) -> AsyncInitVmo {
        Box::pin((self.init_vmo)())
    }

    fn state(&self) -> MutexLockFuture<VmoFileState> {
        self.state.lock()
    }

    fn is_readable(&self) -> bool {
        return self.readable;
    }

    fn is_writable(&self) -> bool {
        return self.writable;
    }

    fn is_executable(&self) -> bool {
        return self.executable;
    }

    fn get_inode(&self) -> u64 {
        return self.inode;
    }
}

impl<InitVmo, InitVmoFuture> DirectoryEntry for VmoFile<InitVmo, InitVmoFuture>
where
    InitVmo: Fn() -> InitVmoFuture + Send + Sync + 'static,
    InitVmoFuture: Future<Output = InitVmoResult> + Send + 'static,
{
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: fio::OpenFlags,
        _mode: u32,
        path: Path,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        if !path.is_empty() {
            send_on_open_with_error(flags, server_end, Status::NOT_DIR);
            return;
        }

        VmoFileConnection::create_connection(scope.clone(), self, flags, server_end);
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(self.inode, fio::DirentType::File)
    }
}
