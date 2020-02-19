// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of an pseudo file with per-connection buffers. These files are backed by
//! asynchronous `init_buffer` and/or `update` callbacks.
//!
//! Each connection to the pseudo file has a unique buffer that all operations through that
//! connection are applied to. This buffer is not synced with the underlying file while the
//! connection is open. If another connection closes and writes new contents to the file, the
//! buffers of the other connections are not updated to reflect it.
//!
//! `init_buffer` callback, if any, is called when a connection to the file is first opened and is
//! used to pre-populate a per-connection buffer that will be used to when serving this file
//! content over this particular connection.  `init_buffer` callback is called only once for a
//! particular connection.
//!
//! `update` callback, if any, is called when the connection is closed if the file content was
//! modified during the whole lifetime of the connection. Modifications are: `write()` calls or
//! opening a file for writing with the `OPEN_FLAG_TRUNCATE` flag set.

#![warn(missing_docs)]

use crate::{
    common::send_on_open_with_error,
    directory::entry::{DirectoryEntry, EntryInfo},
    execution_scope::ExecutionScope,
    file::pcb::connection::{
        AsyncInitBuffer, AsyncUpdate, FileConnection, FileWithPerConnectionBuffer,
    },
    path::Path,
};

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{NodeMarker, DIRENT_TYPE_FILE, INO_UNKNOWN},
    fuchsia_zircon::Status,
    futures::future,
    std::{
        future::Future,
        pin::Pin,
        sync::Arc,
        task::{Context, Poll},
    },
};

#[cfg(test)]
mod tests;

/// This is a "stub" type used by [`read_only`] constructor, when it needs to generate type for the
/// `update` callback that is never used.
pub struct StubUpdateRes;

impl Future for StubUpdateRes {
    type Output = Result<(), Status>;

    fn poll(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Self::Output> {
        Poll::Pending
    }
}

/// Creates a new read-only `AsyncPseudoFile` backed by the specified init_buffer handler.
///
/// The handler is called every time a new connection to the pseudo file is established, and is
/// used to populate a per-connection buffer.
///
/// For more details on this interaction, see the module documentation.
pub fn read_only<InitBuffer, InitBufferRes>(
    init_buffer: InitBuffer,
) -> Arc<AsyncPseudoFile<InitBuffer, InitBufferRes, fn(Vec<u8>) -> StubUpdateRes, StubUpdateRes>>
where
    InitBuffer: Fn() -> InitBufferRes + Send + Sync + 'static,
    InitBufferRes: Future<Output = Result<Vec<u8>, Status>> + Send + Sync + 'static,
{
    AsyncPseudoFile::new(Some(init_buffer), 0, None)
}

/// Creates a new read-only `AsyncPseudoFile` which serves static content.  Also see
/// `read_only_const` which allows you to pass the ownership to the file itself.
pub fn read_only_static<Bytes>(
    bytes: Bytes,
) -> Arc<
    AsyncPseudoFile<
        impl (Fn() -> future::Ready<Result<Vec<u8>, Status>>) + Send + Sync + 'static,
        future::Ready<Result<Vec<u8>, Status>>,
        fn(Vec<u8>) -> StubUpdateRes,
        StubUpdateRes,
    >,
>
where
    Bytes: AsRef<[u8]> + Send + Sync,
{
    read_only(move || future::ready(Ok(bytes.as_ref().to_vec())))
}

/// Create a new read-only `AsyncPseudoFile` which servers a constant content.  The difference with
/// `read_only_static` is that this function takes a run time values that it will own, while
/// `read_only_static` requires a reference to something with a static lifetime.
pub fn read_only_const<Bytes>(
    bytes: Bytes,
) -> Arc<
    AsyncPseudoFile<
        impl (Fn() -> future::Ready<Result<Vec<u8>, Status>>) + Send + Sync + 'static,
        future::Ready<Result<Vec<u8>, Status>>,
        fn(Vec<u8>) -> StubUpdateRes,
        StubUpdateRes,
    >,
>
where
    Bytes: Into<Box<[u8]>>,
{
    let bytes = Arc::new(bytes.into().into_vec());
    read_only(move || future::ready(Ok(bytes.as_ref().clone())))
}

/// This is a "stub" type used by [`write_only`] constructor, when it needs to generate type for
/// the `init_buffer` callback that is never used.
pub struct StubInitBufferRes;

impl Future for StubInitBufferRes {
    type Output = Result<Vec<u8>, Status>;

    fn poll(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Self::Output> {
        Poll::Pending
    }
}

/// Creates a new write-only `AsyncPseudoFile` backed by the specified `update` handler.
///
/// The handler is called when the per-connection buffer content has been updated and the
/// connection is closed. `update` handler will receive the buffer content as the input.
///
/// For more details on this interaction, see the module documentation.
pub fn write_only<Update, UpdateRes>(
    capacity: u64,
    update: Update,
) -> Arc<AsyncPseudoFile<fn() -> StubInitBufferRes, StubInitBufferRes, Update, UpdateRes>>
where
    Update: Fn(Vec<u8>) -> UpdateRes + Send + Sync + 'static,
    UpdateRes: Future<Output = Result<(), Status>> + Send + Sync + 'static,
{
    AsyncPseudoFile::new(None, capacity, Some(update))
}

/// Creates new `AsyncPseudoFile` backed by the specified `init_buffer` and `update` handlers.
///
/// The `init_buffer` handler is called every time a new connection to the pseudo file is
/// established, and is used to populate a per-connection buffer.
///
/// The `update` handler is called when the per-connection buffer content has been updated and the
/// connection is closed. `update` handler will receive the buffer content as the input.
///
/// For more details on these interaction, see the module documentation.
pub fn read_write<InitBuffer, InitBufferRes, Update, UpdateRes>(
    init_buffer: InitBuffer,
    capacity: u64,
    update: Update,
) -> Arc<AsyncPseudoFile<InitBuffer, InitBufferRes, Update, UpdateRes>>
where
    InitBuffer: Fn() -> InitBufferRes + Send + Sync + 'static,
    InitBufferRes: Future<Output = Result<Vec<u8>, Status>> + Send + Sync + 'static,
    Update: Fn(Vec<u8>) -> UpdateRes + Send + Sync + 'static,
    UpdateRes: Future<Output = Result<(), Status>> + Send + Sync + 'static,
{
    AsyncPseudoFile::new(Some(init_buffer), capacity, Some(update))
}

/// Implementation of an asynchronous pseudo file in a virtual file system. This is created by
/// passing `init_buffer` and/or `update` callbacks to the exported constructor functions.
///
/// Futures retuned by the callbacks will be executed by the library using connection specific
/// [`ExecutionScope`].
///
/// See the module documentation for more details.
pub struct AsyncPseudoFile<InitBuffer, InitBufferRes, Update, UpdateRes>
where
    InitBuffer: Fn() -> InitBufferRes + Send + Sync + 'static,
    InitBufferRes: Future<Output = Result<Vec<u8>, Status>> + Send + Sync + 'static,
    Update: Fn(Vec<u8>) -> UpdateRes + Send + Sync + 'static,
    UpdateRes: Future<Output = Result<(), Status>> + Send + Sync + 'static,
{
    init_buffer: Option<InitBuffer>,
    capacity: u64,
    update: Option<Update>,
}

impl<InitBuffer, InitBufferRes, Update, UpdateRes>
    AsyncPseudoFile<InitBuffer, InitBufferRes, Update, UpdateRes>
where
    InitBuffer: Fn() -> InitBufferRes + Send + Sync + 'static,
    InitBufferRes: Future<Output = Result<Vec<u8>, Status>> + Send + Sync + 'static,
    Update: Fn(Vec<u8>) -> UpdateRes + Send + Sync + 'static,
    UpdateRes: Future<Output = Result<(), Status>> + Send + Sync + 'static,
{
    fn new(init_buffer: Option<InitBuffer>, capacity: u64, update: Option<Update>) -> Arc<Self> {
        Arc::new(AsyncPseudoFile { init_buffer, capacity, update })
    }
}

impl<InitBuffer, InitBufferRes, Update, UpdateRes> FileWithPerConnectionBuffer
    for AsyncPseudoFile<InitBuffer, InitBufferRes, Update, UpdateRes>
where
    InitBuffer: Fn() -> InitBufferRes + Send + Sync + 'static,
    InitBufferRes: Future<Output = Result<Vec<u8>, Status>> + Send + Sync + 'static,
    Update: Fn(Vec<u8>) -> UpdateRes + Send + Sync + 'static,
    UpdateRes: Future<Output = Result<(), Status>> + Send + Sync + 'static,
{
    fn init_buffer(self: Arc<Self>) -> AsyncInitBuffer {
        match &self.init_buffer {
            None => {
                if cfg!(debug_assertions) {
                    panic!("`init_buffer` called for a non-readable file")
                } else {
                    AsyncInitBuffer::Immediate(Ok(vec![]))
                }
            }
            Some(init_buffer) => AsyncInitBuffer::Future(Box::pin(init_buffer())),
        }
    }

    fn update(self: Arc<Self>, buffer: Vec<u8>) -> AsyncUpdate {
        match &self.update {
            None => {
                if cfg!(debug_assertions) {
                    panic!("`update` called for a non-writable file")
                } else {
                    AsyncUpdate::Immediate(Ok(()))
                }
            }
            Some(update) => AsyncUpdate::Future(Box::pin(update(buffer))),
        }
    }
}

impl<InitBuffer, InitBufferRes, Update, UpdateRes> DirectoryEntry
    for AsyncPseudoFile<InitBuffer, InitBufferRes, Update, UpdateRes>
where
    InitBuffer: Fn() -> InitBufferRes + Send + Sync + 'static,
    InitBufferRes: Future<Output = Result<Vec<u8>, Status>> + Send + Sync + 'static,
    Update: Fn(Vec<u8>) -> UpdateRes + Send + Sync + 'static,
    UpdateRes: Future<Output = Result<(), Status>> + Send + Sync + 'static,
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

        let readable = self.init_buffer.is_some();
        let writable = self.update.is_some();
        let capacity = self.capacity;
        FileConnection::create_connection(
            scope.clone(),
            self,
            flags,
            mode,
            server_end,
            readable,
            writable,
            capacity,
        );
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)
    }

    fn can_hardlink(&self) -> bool {
        true
    }
}
