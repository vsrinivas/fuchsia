// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of a file backed by a VMO buffer shared by all the file connections.  From the
//! library user side, these files are backed by asynchronous `init_vmo` and/or `consume_vmo`
//! callbacks.
//!
//! Connections to this kind of file synchronize and perform operations on a shared VMO initially
//! provided by the `init_vmo` callback.   When the last connection is closed, `consume_vmo` is
//! called.  If `consume_vmo` is not provided, then the VMO is just dropped.
//!
//! `init_vmo` callback is called when the first connection to the file is established and is
//! responsible for providing a VMO to be used by all the connection.
//!
//! `consume_vmo` callback, if any, is called when the last active connection to the file is closed.

#![warn(missing_docs)]

pub mod test_utils;

#[cfg(test)]
mod tests;

use crate::{
    common::send_on_open_with_error,
    directory::entry::{DirectoryEntry, EntryInfo},
    execution_scope::ExecutionScope,
    file::vmo::connection::{AsyncConsumeVmo, AsyncInitVmo, FileConnection, FileConnectionApi},
    path::Path,
};

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{NodeMarker, DIRENT_TYPE_FILE, INO_UNKNOWN},
    fuchsia_zircon::{Status, Vmo},
    futures::lock::{Mutex, MutexLockFuture},
    std::{future::Future, pin::Pin, sync::Arc, task::{Context, Poll}},
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

/// `consume_vmo` will be ran in the scope of the last connection to the file, but the connection
/// itself might be closed by the time `consume_vmo` completes.  This result does not have an
/// embedded error information in it, as it does not make sense to report errors only on the last
/// connection, as this introduces race conditions (which connection is the last one?).
pub type ConsumeVmoResult = ();

/// This is a "stub" type used by [`read_only`] constructor, when it needs to generate type for the
/// `consume_vmo` callback that is never used.
pub struct StubConsumeVmoRes;

impl Future for StubConsumeVmoRes {
    type Output = ConsumeVmoResult;

    fn poll(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Self::Output> {
        Poll::Pending
    }
}

/// Creates a new read-only `AsyncFile` backed by the specified `init_vmo` handler.
///
/// The `init_vmo` handler is called to initialize a VMO for the very first connection to the file.
/// Should all the connection be closed the VMO is discarded, and any new connections will cause
/// `init_vmo` to be called again.
///
/// New connections are only allowed when they specify "read-only" access to the file content.
///
/// For more details on this interaction, see the module documentation.
pub fn read_only<InitVmo, InitVmoFuture>(
    init_vmo: InitVmo,
) -> Arc<AsyncFile<InitVmo, InitVmoFuture, fn(Vmo) -> StubConsumeVmoRes, StubConsumeVmoRes>>
where
    InitVmo: Fn() -> InitVmoFuture + Send + Sync + 'static,
    InitVmoFuture: Future<Output = InitVmoResult> + Send + 'static,
{
    AsyncFile::new(init_vmo, None, true, false)
}

/// Creates a new write-only `AsyncFile` backed by the specified `init_vmo` and `consume_vmo`
/// handlers.
///
/// The `init_vmo` handler is called to initialize a VMO for the very first connection to the file.
/// Should all the connection be closed the VMO is discarded, and any new connections will cause
/// `init_vmo` to be called again.
///
/// The `consume_vmo` handler is called when the last connection to the file is closed and it can
/// process the VMO in some way.  Should a new connection be opened, `init_vmo` will be invoked to
/// provide a VMO backing this new connection.
///
/// New connections are only allowed when they specify "write-only" access to the file content.
///
/// For more details on these interaction, see the module documentation.
pub fn write_only<InitVmo, InitVmoFuture, ConsumeVmo, ConsumeVmoFuture>(
    init_vmo: InitVmo,
    consume_vmo: ConsumeVmo,
) -> Arc<AsyncFile<InitVmo, InitVmoFuture, ConsumeVmo, ConsumeVmoFuture>>
where
    InitVmo: Fn() -> InitVmoFuture + Send + Sync + 'static,
    InitVmoFuture: Future<Output = InitVmoResult> + Send + 'static,
    ConsumeVmo: Fn(Vmo) -> ConsumeVmoFuture + Send + Sync + 'static,
    ConsumeVmoFuture: Future<Output = ConsumeVmoResult> + Send + 'static,
{
    AsyncFile::new(init_vmo, Some(consume_vmo), false, true)
}

/// Creates new `AsyncFile` backed by the specified `init_vmo` and `consume_vmo` handlers.
///
/// The `init_vmo` handler is called to initialize a VMO for the very first connection to the file.
/// Should all the connection be closed the VMO is discarded, and any new connections will cause
/// `init_vmo` to be called again.
///
/// The `consume_vmo` handler is called when the last connection to the file is closed and it can
/// process the VMO in some way.  Should a new connection be opened, `init_vmo` will be invoked to
/// provide a VMO backing this new connection.
///
/// New connections may specify any kind of access to the file content.
///
/// For more details on these interaction, see the module documentation.
pub fn read_write<InitVmo, InitVmoFuture, ConsumeVmo, ConsumeVmoFuture>(
    init_vmo: InitVmo,
    consume_vmo: ConsumeVmo,
) -> Arc<AsyncFile<InitVmo, InitVmoFuture, ConsumeVmo, ConsumeVmoFuture>>
where
    InitVmo: Fn() -> InitVmoFuture + Send + Sync + 'static,
    InitVmoFuture: Future<Output = InitVmoResult> + Send + 'static,
    ConsumeVmo: Fn(Vmo) -> ConsumeVmoFuture + Send + Sync + 'static,
    ConsumeVmoFuture: Future<Output = ConsumeVmoResult> + Send + 'static,
{
    AsyncFile::new(init_vmo, Some(consume_vmo), true, true)
}

/// Implementation of an asynchronous file in a virtual file system. This is created by passing
/// `init_vmo` and/or `consume_vmo` callbacks to the exported constructor functions.
///
/// Futures retuned by the callbacks will be executed by the library using connection specific
/// [`ExecutionScope`].
///
/// See the module documentation for more details.
pub struct AsyncFile<InitVmo, InitVmoFuture, ConsumeVmo, ConsumeVmoFuture>
where
    InitVmo: Fn() -> InitVmoFuture + Send + Sync + 'static,
    InitVmoFuture: Future<Output = InitVmoResult> + Send + 'static,
    ConsumeVmo: Fn(Vmo) -> ConsumeVmoFuture + Send + Sync + 'static,
    ConsumeVmoFuture: Future<Output = ConsumeVmoResult> + Send + 'static,
{
    init_vmo: InitVmo,

    /// When the last connection to the file is gone, this callback will be used to "recycle" the
    /// VMO.  If the file is writable, then the callback can update the file content.  If the
    /// callback is absent, the VMO handle is just released.
    consume_vmo: Option<ConsumeVmo>,

    /// Specifies if the file is readable.  `init_vmo` is always invoked, even for non-readable
    /// VMOs.  So, unlike `pcb::AsyncFile`, we need a separate flag to track the readability state.
    readable: bool,

    /// Specifies if the file is writable.  `consume_vmo` might be provided even for a read-only
    /// file in order to reprocess the VMO when all the connections are gone.  It is an error to
    /// have a writable file without a `consume_vmo` though.
    writable: bool,

    // File connections share state with the file itself.
    // TODO: It should be `pub(in super::connection)` but the compiler claims, `super` does not
    // contain a `connection`.  Neither `pub(in create:vmo::connection)` works.
    pub(super) state: Mutex<AsyncFileState>,
}

/// State shared between all the connections to a file, across all execution scopes.
// TODO: It should be `pub(in super::connection)` but the compiler claims, `super` does not contain
// a `connection`.  Neither the `pub(in create:vmo::connection)` works.
pub(super) enum AsyncFileState {
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

        /// Number of active connections to the file.  When this drops to 0, we give away the VMO
        /// to the `consume_vmo` handler (if any), and then switch to the `Uninitialized` state.
        connection_count: u64,
    },
}

impl<InitVmo, InitVmoFuture, ConsumeVmo, ConsumeVmoFuture>
    AsyncFile<InitVmo, InitVmoFuture, ConsumeVmo, ConsumeVmoFuture>
where
    InitVmo: Fn() -> InitVmoFuture + Send + Sync + 'static,
    InitVmoFuture: Future<Output = InitVmoResult> + Send + 'static,
    ConsumeVmo: Fn(Vmo) -> ConsumeVmoFuture + Send + Sync + 'static,
    ConsumeVmoFuture: Future<Output = ConsumeVmoResult> + Send + 'static,
{
    fn new(
        init_vmo: InitVmo,
        consume_vmo: Option<ConsumeVmo>,
        readable: bool,
        writable: bool,
    ) -> Arc<Self> {
        // A writable file has to have a `consume_vmo` handler.  Otherwise we may lose updates
        // without realizing it.
        assert!(!writable || consume_vmo.is_some());

        Arc::new(AsyncFile {
            init_vmo,
            consume_vmo,
            readable,
            writable,
            state: Mutex::new(AsyncFileState::Uninitialized),
        })
    }
}

impl<InitVmo, InitVmoFuture, ConsumeVmo, ConsumeVmoFuture> FileConnectionApi
    for AsyncFile<InitVmo, InitVmoFuture, ConsumeVmo, ConsumeVmoFuture>
where
    InitVmo: Fn() -> InitVmoFuture + Send + Sync + 'static,
    InitVmoFuture: Future<Output = InitVmoResult> + Send + 'static,
    ConsumeVmo: Fn(Vmo) -> ConsumeVmoFuture + Send + Sync + 'static,
    ConsumeVmoFuture: Future<Output = ConsumeVmoResult> + Send + 'static,
{
    fn init_vmo(self: Arc<Self>) -> AsyncInitVmo {
        Box::pin((self.init_vmo)())
    }

    fn consume_vmo(self: Arc<Self>, vmo: Vmo) -> AsyncConsumeVmo {
        match &self.consume_vmo {
            None => {
                if self.writable && cfg!(debug_assertions) {
                    panic!("`consume_vmo` is None for a writable file")
                } else {
                    AsyncConsumeVmo::Immediate(())
                }
            }
            Some(consume_vmo) => AsyncConsumeVmo::Future(Box::pin(consume_vmo(vmo))),
        }
    }

    fn state(&self) -> MutexLockFuture<AsyncFileState> {
        self.state.lock()
    }
}

impl<InitVmo, InitVmoFuture, ConsumeVmo, ConsumeVmoFuture> DirectoryEntry
    for AsyncFile<InitVmo, InitVmoFuture, ConsumeVmo, ConsumeVmoFuture>
where
    InitVmo: Fn() -> InitVmoFuture + Send + Sync + 'static,
    InitVmoFuture: Future<Output = InitVmoResult> + Send + 'static,
    ConsumeVmo: Fn(Vmo) -> ConsumeVmoFuture + Send + Sync + 'static,
    ConsumeVmoFuture: Future<Output = ConsumeVmoResult> + Send + 'static,
{
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        path: Path,
        server_end: ServerEnd<NodeMarker>,
    ) {
        if !path.is_empty() {
            send_on_open_with_error(flags, server_end, Status::NOT_DIR);
            return;
        }

        let readable = self.readable;
        let writable = self.writable;
        FileConnection::create_connection(
            scope.clone(),
            self,
            flags,
            mode,
            readable,
            writable,
            server_end,
        );
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)
    }

    fn can_hardlink(&self) -> bool {
        true
    }
}
