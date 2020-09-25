// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of a pseudo file trait backed by read and/or write callbacks and a buffer.
//!
//! Read callback, if any, is called when the connection to the file is first opened and
//! pre-populates a buffer that will be used to when serving this file content over this particular
//! connection.  Read callback is ever called once.
//!
//! Write callback, if any, is called when the connection is closed if the file content was ever
//! modified while the connection was open.  Modifications are: write() calls or opening a file for
//! writing with the OPEN_FLAG_TRUNCATE flag set.
//!
//! First write operation will reset the seek position and empty the buffer.  Any subsequent read
//! operations will read from the same buffer, returning the data that was already written.  This
//! quirk in behaviour is in order to simplify the read()/write() scenarios.
//!
//! Main use case for the pseudo files that are both readable and writeable is the exposure of the
//! component configuration parameters.  In this case when the configuration value is read it is
//! always presented in a canonical format, but when it is written, multiple formats could be used.
//! As a consequence, when the new value length could shorter than the current value (and when
//! different formats are supported it is more likely), besides just writing new file content one
//! would also need to truncate the file.  As we treat our configuration values as atomic, we
//! consider the scenario of an incremental edit less likely, so we truncate automatically.
//!
//! Thinking again about the above, it seems like an unnecessary complication.  When someone wants
//! to just update the value, they can open with OPEN_FLAG_TRUNCATE flag (as does shell when using
//! output redirection) and when the scenario is to read and to write, there is a Truncate() call
//! that sets new file size.  This would remove the quirk and make the file behave more like all
//! the other files.

#![warn(missing_docs)]

use crate::{
    common::{inherit_rights_for_clone, send_on_open_with_error},
    directory::entry::{DirectoryEntry, EntryInfo},
    file::{
        connection::{ConnectionState, FileConnection},
        DEFAULT_READ_ONLY_PROTECTION_ATTRIBUTES, DEFAULT_READ_WRITE_PROTECTION_ATTRIBUTES,
        DEFAULT_WRITE_ONLY_PROTECTION_ATTRIBUTES,
    },
};

use {
    anyhow::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        FileRequest, NodeMarker, DIRENT_TYPE_FILE, INO_UNKNOWN, MODE_PROTECTION_MASK,
        OPEN_FLAG_TRUNCATE, OPEN_RIGHT_READABLE,
    },
    fuchsia_zircon::Status,
    futures::{
        future::FusedFuture,
        stream::{FuturesUnordered, StreamExt, StreamFuture},
    },
    std::{
        future::Future,
        marker::Unpin,
        pin::Pin,
        task::{Context, Poll},
    },
    void::Void,
};

// TODO: When trait aliases are implemented (rust-lang/rfcs#1733)
// trait OnReadHandler = FnMut() -> Result<Vec<u8>, Status>;
// trait OnWriteHandler = FnMut(Vec<u8>) -> Result<(), Status>;

/// Creates a new read-only `PseudoFile` backed by the specified read handler.
///
/// The handler is called every time a read operation is performed on the file.  It is only allowed
/// to read at offset 0, and all of the content returned by the handler is returned by the read
/// operation.  Subsequent reads act the same - there is no seek position, nor ability to read
/// content in chunks.
pub fn read_only<OnRead>(on_read: OnRead) -> PseudoFile<OnRead, fn(Vec<u8>) -> Result<(), Status>>
where
    OnRead: FnMut() -> Result<Vec<u8>, Status> + Send,
{
    read_only_attr(DEFAULT_READ_ONLY_PROTECTION_ATTRIBUTES, on_read)
}

/// See [`read_only()`].  Wraps the callback, allowing it to return a String instead of a Vec<u8>,
/// but otherwise behaves identical to [`read_only()`].
pub fn read_only_str<OnReadStr>(
    mut on_read: OnReadStr,
) -> PseudoFile<impl FnMut() -> Result<Vec<u8>, Status> + Send, fn(Vec<u8>) -> Result<(), Status>>
where
    OnReadStr: FnMut() -> Result<String, Status> + Send,
{
    PseudoFile::<_, fn(Vec<u8>) -> Result<(), Status>>::new(
        DEFAULT_READ_ONLY_PROTECTION_ATTRIBUTES,
        Some(move || on_read().map(|content| content.into_bytes())),
        0,
        None,
    )
}

/// See [`read_only()`].  Conveneient wrapper for [`read_only()`] that builds a file node out of a
/// static string.
pub fn read_only_static(
    content: &'static str,
) -> PseudoFile<impl FnMut() -> Result<Vec<u8>, Status> + Send, fn(Vec<u8>) -> Result<(), Status>> {
    PseudoFile::<_, fn(Vec<u8>) -> Result<(), Status>>::new(
        DEFAULT_READ_ONLY_PROTECTION_ATTRIBUTES,
        Some(move || Ok(content.into())),
        0,
        None,
    )
}

/// Same as [`read_only()`] but also allows to select custom attributes for the POSIX emulation
/// layer.  Note that only the MODE_PROTECTION_MASK part of the protection_attributes argument will
/// be stored.
pub fn read_only_attr<OnRead>(
    protection_attributes: u32,
    on_read: OnRead,
) -> PseudoFile<OnRead, fn(Vec<u8>) -> Result<(), Status>>
where
    OnRead: FnMut() -> Result<Vec<u8>, Status> + Send,
{
    PseudoFile::<_, fn(Vec<u8>) -> Result<(), Status>>::new(
        protection_attributes & MODE_PROTECTION_MASK,
        Some(on_read),
        0,
        None,
    )
}

/// Creates a new write-only `PseudoFile` backed by the specified write handler.
///
/// The handler is called every time a write operation is performed on the file.  It is only
/// allowed to write at offset 0, and all of the new content should be provided to a single write
/// operation.  Subsequent writes act the same - there is no seek position, nor ability to write
/// content in chunks.
pub fn write_only<OnWrite>(
    capacity: u64,
    on_write: OnWrite,
) -> PseudoFile<fn() -> Result<Vec<u8>, Status>, OnWrite>
where
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status> + Send,
{
    write_only_attr(DEFAULT_WRITE_ONLY_PROTECTION_ATTRIBUTES, capacity, on_write)
}

/// See [`write_only()`].  Only allows valid UTF-8 content to be written into the file.  Written
/// bytes are converted into a string instance an std::str::from_utf8() call, and the passed to the
/// handler.
pub fn write_only_str<OnWriteStr>(
    capacity: u64,
    mut on_write: OnWriteStr,
) -> PseudoFile<fn() -> Result<Vec<u8>, Status>, impl FnMut(Vec<u8>) -> Result<(), Status> + Send>
where
    OnWriteStr: FnMut(String) -> Result<(), Status> + Send,
{
    PseudoFile::<fn() -> Result<Vec<u8>, Status>, _>::new(
        DEFAULT_WRITE_ONLY_PROTECTION_ATTRIBUTES,
        None,
        capacity,
        Some(move |bytes: Vec<u8>| match String::from_utf8(bytes) {
            Ok(content) => on_write(content),
            Err(_) => Err(Status::INVALID_ARGS),
        }),
    )
}

/// Same as [`write_only()`] but also allows to select custom attributes for the POSIX emulation
/// layer.  Note that only the MODE_PROTECTION_MASK part of the protection_attributes argument will
/// be stored.
pub fn write_only_attr<OnWrite>(
    protection_attributes: u32,
    capacity: u64,
    on_write: OnWrite,
) -> PseudoFile<fn() -> Result<Vec<u8>, Status>, OnWrite>
where
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status> + Send,
{
    PseudoFile::<fn() -> Result<Vec<u8>, Status>, _>::new(
        protection_attributes & MODE_PROTECTION_MASK,
        None,
        capacity,
        Some(on_write),
    )
}

/// Creates new `PseudoFile` backed by the specified read and write handlers.
///
/// The read handler is called every time a read operation is performed on the file.  It is only
/// allowed to read at offset 0, and all of the content returned by the handler is returned by the
/// read operation.  Subsequent reads act the same - there is no seek position, nor ability to read
/// content in chunks.
///
/// The write handler is called every time a write operation is performed on the file.  It is only
/// allowed to write at offset 0, and all of the new content should be provided to a single write
/// operation.  Subsequent writes act the same - there is no seek position, nor ability to write
/// content in chunks.
pub fn read_write<OnRead, OnWrite>(
    on_read: OnRead,
    capacity: u64,
    on_write: OnWrite,
) -> PseudoFile<OnRead, OnWrite>
where
    OnRead: FnMut() -> Result<Vec<u8>, Status> + Send,
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status> + Send,
{
    read_write_attr(DEFAULT_READ_WRITE_PROTECTION_ATTRIBUTES, on_read, capacity, on_write)
}

/// See [`read_write()`].  Wraps the read callback, allowing it to return a [`String`] instead of a
/// [`Vec<u8>`].  Wraps the write callback, only allowing valid UTF-8 content to be written into
/// the file.  Written bytes are converted into a string instance an [`std::str::from_utf8()`]
/// call, and the passed to the handler.
/// In every other aspect behaves just like [`read_write()`].
pub fn read_write_str<OnReadStr, OnWriteStr>(
    mut on_read: OnReadStr,
    capacity: u64,
    mut on_write: OnWriteStr,
) -> PseudoFile<
    impl FnMut() -> Result<Vec<u8>, Status> + Send,
    impl FnMut(Vec<u8>) -> Result<(), Status> + Send,
>
where
    OnReadStr: FnMut() -> Result<String, Status> + Send,
    OnWriteStr: FnMut(String) -> Result<(), Status> + Send,
{
    PseudoFile::new(
        DEFAULT_READ_WRITE_PROTECTION_ATTRIBUTES,
        Some(move || on_read().map(|content| content.into_bytes())),
        capacity,
        Some(move |bytes: Vec<u8>| match String::from_utf8(bytes) {
            Ok(content) => on_write(content),
            Err(_) => Err(Status::INVALID_ARGS),
        }),
    )
}

/// Same as [`read_write()`] but also allows to select custom attributes for the POSIX emulation
/// layer.  Note that only the MODE_PROTECTION_MASK part of the protection_attributes argument will
/// be stored.
pub fn read_write_attr<OnRead, OnWrite>(
    protection_attributes: u32,
    on_read: OnRead,
    capacity: u64,
    on_write: OnWrite,
) -> PseudoFile<OnRead, OnWrite>
where
    OnRead: FnMut() -> Result<Vec<u8>, Status> + Send,
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status> + Send,
{
    PseudoFile::new(
        protection_attributes & MODE_PROTECTION_MASK,
        Some(on_read),
        capacity,
        Some(on_write),
    )
}

/// An implementation of a pseudo file, as described by the module level documentation - [`file`].
// TODO I think we should split this into 3 different implementations: read_only, write_only and
// read_write.  We can avoid code duplication by composing all 3 out of a common base with
// additional functionality specific to each one.  The upside of having 3 different types would be
// better return types for the `read_only`, `write_only` and `read_write`, constructors.  And we
// would be able to remove some of the conditional logic related to `Option`.
pub struct PseudoFile<OnRead, OnWrite>
where
    OnRead: FnMut() -> Result<Vec<u8>, Status> + Send,
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status> + Send,
{
    /// MODE_PROTECTION_MASK attributes returned by this file through io.fild:Node::GetAttr.  They
    /// have no meaning for the file operation itself, but may have consequences to the POSIX
    /// emulation layer - for example, it makes sense to remove the read flags from a read-only
    /// file.  This field should only have set bits in the MODE_PROTECTION_MASK part.
    protection_attributes: u32,

    /// A handler to be invoked to populate the content buffer when a connection to this file is
    /// opened.
    on_read: Option<OnRead>,

    /// Maximum size the buffer that holds the value written into this file can grow to.  When the
    /// buffer is populated by the [`on_read`] handler, this restriction is not enforced.  The
    /// maximum size of the buffer passed into [`on_write`] is the maximum of the size of the
    /// buffer that [`on_read`] have returned and this value.
    capacity: u64,

    /// A handler to be invoked to "update" the file content, if it was modified during a
    /// connection lifetime.
    on_write: Option<OnWrite>,

    /// All the currently open connections for this file.
    connections: FuturesUnordered<StreamFuture<FileConnection>>,
}

impl<OnRead, OnWrite> PseudoFile<OnRead, OnWrite>
where
    OnRead: FnMut() -> Result<Vec<u8>, Status> + Send,
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status> + Send,
{
    fn new(
        protection_attributes: u32,
        on_read: Option<OnRead>,
        capacity: u64,
        on_write: Option<OnWrite>,
    ) -> Self {
        PseudoFile {
            protection_attributes,
            on_read,
            capacity,
            on_write,
            connections: FuturesUnordered::new(),
        }
    }

    /// Attaches a new connection, client end `server_end`, to this object.  Any error are reported
    /// as `OnOpen` events on the `server_end` itself.
    fn add_connection(&mut self, flags: u32, mode: u32, server_end: ServerEnd<NodeMarker>) {
        if let Some(conn) = FileConnection::connect(
            flags,
            mode,
            self.protection_attributes,
            server_end,
            self.on_read.is_some(),
            self.on_write.is_some(),
            self.capacity,
            |flags| self.init_buffer(flags),
        ) {
            self.connections.push(conn);
        }
    }

    fn handle_request(
        &mut self,
        req: FileRequest,
        connection: &mut FileConnection,
    ) -> Result<ConnectionState, Error> {
        match req {
            FileRequest::Clone { flags, object, control_handle: _ } => {
                self.handle_clone(connection.flags, flags, object);
                Ok(ConnectionState::Alive)
            }
            FileRequest::Close { responder } => {
                self.handle_close(connection, |status| responder.send(status.into_raw()))?;
                Ok(ConnectionState::Closed)
            }
            _ => {
                connection.handle_request(req)?;
                Ok(ConnectionState::Alive)
            }
        }
    }

    fn init_buffer(&mut self, flags: u32) -> Result<(Vec<u8>, bool), Status> {
        // No point in calling the read handler for non-readable files.
        if flags & OPEN_RIGHT_READABLE == 0 {
            return Ok((vec![], false));
        }

        match self.on_read {
            None => Ok((vec![], false)),
            Some(ref mut on_read) => {
                // No point in calling the read hander, if we want to erase all of the content
                // right away, but we need to remember the content was overwritten.
                if flags & OPEN_FLAG_TRUNCATE != 0 {
                    Ok((vec![], true))
                } else {
                    let buffer = on_read()?;
                    Ok((buffer, false))
                }
            }
        }
    }

    fn handle_clone(&mut self, parent_flags: u32, flags: u32, server_end: ServerEnd<NodeMarker>) {
        let flags = match inherit_rights_for_clone(parent_flags, flags) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        self.add_connection(flags, 0, server_end);
    }

    fn handle_close<R>(
        &mut self,
        connection: &mut FileConnection,
        responder: R,
    ) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status) -> Result<(), fidl::Error>,
    {
        match &mut self.on_write {
            None => responder(Status::OK),
            Some(on_write) => match connection.handle_close(on_write, Ok(())) {
                Ok(()) => responder(Status::OK),
                Err(status) => responder(status),
            },
        }
    }
}

impl<OnRead, OnWrite> DirectoryEntry for PseudoFile<OnRead, OnWrite>
where
    OnRead: FnMut() -> Result<Vec<u8>, Status> + Send,
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status> + Send,
{
    fn open(
        &mut self,
        flags: u32,
        mode: u32,
        path: &mut dyn Iterator<Item = &str>,
        server_end: ServerEnd<NodeMarker>,
    ) {
        if let Some(_) = path.next() {
            send_on_open_with_error(flags, server_end, Status::NOT_DIR);
            return;
        }

        self.add_connection(flags, mode, server_end);
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)
    }
}

impl<OnRead, OnWrite> Unpin for PseudoFile<OnRead, OnWrite>
where
    OnRead: FnMut() -> Result<Vec<u8>, Status> + Send,
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status> + Send,
{
}

impl<OnRead, OnWrite> FusedFuture for PseudoFile<OnRead, OnWrite>
where
    OnRead: FnMut() -> Result<Vec<u8>, Status> + Send,
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status> + Send,
{
    fn is_terminated(&self) -> bool {
        // The `PseudoFileImpl` never completes, but once there are no more connections, it is
        // blocked until more connections are added. If the object currently polling a `PseudoFile`
        // with an empty set of connections is blocked on the `PseudoFile` completing, it will never
        // terminate.
        self.connections.len() == 0
    }
}

impl<OnRead, OnWrite> Future for PseudoFile<OnRead, OnWrite>
where
    OnRead: FnMut() -> Result<Vec<u8>, Status> + Send,
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status> + Send,
{
    type Output = Void;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        // NOTE See `crate::directory::Simple::poll` for the discussion on why we need a loop here.

        loop {
            match self.connections.poll_next_unpin(cx) {
                Poll::Ready(Some((maybe_request, mut connection))) => {
                    let state = match maybe_request {
                        Some(Ok(request)) => self
                            .handle_request(request, &mut connection)
                            // If an error occurred while processing a request we will just close
                            // the connection, effectively closing the underlying channel in the
                            // destructor.  Make one last attempt to call `handle_close` in case we
                            // have any modifications in the connection buffer.
                            .unwrap_or(ConnectionState::Dropped),
                        Some(Err(_)) | None => {
                            // If the connection was closed by the peer (`None`) or an error has
                            // occurred (`Some(Err)`), we still need to make sure we run the
                            // `on_write` handler.  There is nowhere to report any errors to, so we
                            // just ignore those.
                            ConnectionState::Dropped
                        }
                    };

                    match state {
                        ConnectionState::Alive => self.connections.push(connection.into_future()),
                        ConnectionState::Closed => (),
                        ConnectionState::Dropped => {
                            let _ = self.handle_close(&mut connection, |_status| Ok(()));
                        }
                    }
                }
                // Even when we have no connections any more we still report Pending state, as we
                // may get more connections open in the future.
                Poll::Ready(None) | Poll::Pending => break,
            }
        }

        Poll::Pending
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use {
        crate::file::test_utils::{
            run_server_client, run_server_client_with_open_requests_channel,
            run_server_client_with_open_requests_channel_and_executor,
        },
        fidl::endpoints::{create_proxy, ServerEnd},
        fidl_fuchsia_io::{
            FileEvent, FileMarker, FileObject, NodeAttributes, NodeInfo, INO_UNKNOWN,
            MODE_TYPE_FILE, OPEN_FLAG_DESCRIBE, OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_POSIX,
            OPEN_FLAG_TRUNCATE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
        },
        fuchsia_async as fasync,
        fuchsia_zircon::{sys::ZX_OK, Status},
        futures::channel::{mpsc, oneshot},
        futures::future::join,
        futures::SinkExt,
        libc::{S_IRGRP, S_IROTH, S_IRUSR, S_IWGRP, S_IWOTH, S_IWUSR, S_IXGRP, S_IXOTH, S_IXUSR},
        std::sync::atomic::{AtomicUsize, Ordering},
    };

    #[test]
    fn read_only_read() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(|| Ok(b"Read only test".to_vec())),
            |proxy| async move {
                assert_read!(proxy, "Read only test");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn get_buffer() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only_static("Get buffer test"),
            |proxy| async move {
                assert_get_buffer!(proxy, "Get buffer test");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn get_buffer_with_rw_file() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write_str(
                || Ok("Hello".to_string()),
                100,
                |_| panic!("file shouldn't be written to"),
            ),
            |proxy| async move {
                assert_get_buffer_err!(proxy, OPEN_RIGHT_READABLE, Status::NOT_SUPPORTED);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn get_buffer_writable_with_readonly_file() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only_static("Get buffer test"),
            |proxy| async move {
                assert_get_buffer_err!(proxy, OPEN_RIGHT_WRITABLE, Status::ACCESS_DENIED);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn get_buffer_readable_with_writable_file() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only_str(100, |_| panic!("file shouldn't be written to")),
            |proxy| async move {
                assert_get_buffer_err!(proxy, OPEN_RIGHT_READABLE, Status::NOT_SUPPORTED);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn get_buffer_writable_with_writable_file() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only_str(100, |_| panic!("file shouldn't be written to")),
            |proxy| async move {
                assert_get_buffer_err!(proxy, OPEN_RIGHT_WRITABLE, Status::NOT_SUPPORTED);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn get_buffer_writable_with_rw_file() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write_str(
                || Ok("Hello".to_string()),
                100,
                |_| panic!("file shouldn't be written to"),
            ),
            |proxy| async move {
                assert_get_buffer_err!(proxy, OPEN_RIGHT_WRITABLE, Status::NOT_SUPPORTED);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_only_ignore_posix_flag() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_FLAG_POSIX,
            read_write(
                || Ok(b"Content".to_vec()),
                100,
                |_content| {
                    panic!("OPEN_FLAG_POSIX should be ignored for files");
                },
            ),
            |proxy| async move {
                assert_read!(proxy, "Content");
                assert_write_err!(proxy, "Can write", Status::ACCESS_DENIED);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_only_read_no_status() {
        let exec = fasync::Executor::new().expect("Executor creation failed");

        run_server_client_with_open_requests_channel_and_executor(
            exec,
            read_only_static("Read only test"),
            |mut open_sender| {
                async move {
                    let (proxy, server_end) = create_proxy::<FileMarker>()
                        .expect("Failed to create connection endpoints");

                    let flags = OPEN_RIGHT_READABLE;
                    open_sender.send((flags, 0, server_end)).await.unwrap();
                    assert_no_event!(proxy);
                    // NOTE: logic added after `assert_no_event!` will not currently be run. this test
                    // will need to be updated after fxbug.dev/33709 is completed.
                }
            },
            |run_until_stalled_assert| {
                // we don't expect the server to complete, it's waiting for an event that will never
                // come (and we don't ever actually close the file).
                run_until_stalled_assert(false);
            },
        );
    }

    #[test]
    fn read_only_read_with_describe() {
        run_server_client_with_open_requests_channel(
            read_only_static("Read only test"),
            |mut open_sender| async move {
                let (proxy, server_end) =
                    create_proxy::<FileMarker>().expect("Failed to create connection endpoints");

                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                open_sender.send((flags, 0, server_end)).await.unwrap();
                assert_event!(proxy, FileEvent::OnOpen_ { s, info }, {
                    assert_eq!(s, ZX_OK);
                    assert_eq!(
                        info,
                        Some(Box::new(NodeInfo::File(FileObject { event: None, stream: None })))
                    );
                });
            },
        );
    }

    #[test]
    fn read_only_str_read() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only_str(|| Ok(String::from("Read only str test"))),
            |proxy| async move {
                assert_read!(proxy, "Read only str test");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_only_static_read() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only_static("Easy strings"),
            |proxy| async move {
                assert_read!(proxy, "Easy strings");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn write_only_write() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, |content| {
                assert_eq!(&*content, b"Write only test");
                Ok(())
            }),
            |proxy| async move {
                assert_write!(proxy, "Write only test");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn write_only_str_write() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only_str(100, |content| {
                assert_eq!(&*content, "Write only test");
                Ok(())
            }),
            |proxy| async move {
                assert_write!(proxy, "Write only test");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_write_read_and_write() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Hello".to_vec()),
                100,
                |content| {
                    assert_eq!(*&content, b"Hello, world!");
                    Ok(())
                },
            ),
            |proxy| async move {
                assert_read!(proxy, "Hello");
                assert_write!(proxy, ", world!");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_write_str_read_and_write() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write_str(
                || Ok("Hello".to_string()),
                100,
                |content| {
                    assert_eq!(*&content, "Hello, world!");
                    Ok(())
                },
            ),
            |proxy| async move {
                assert_read!(proxy, "Hello");
                assert_write!(proxy, ", world!");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_twice() {
        let mut read_attempt = 0;

        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(|| {
                read_attempt += 1;
                match read_attempt {
                    1 => Ok(b"State one".to_vec()),
                    _ => panic!("Third read() call."),
                }
            }),
            |proxy| async move {
                assert_read!(proxy, "State one");
                assert_seek!(proxy, 0, Start);
                assert_read!(proxy, "State one");
                assert_close!(proxy);
            },
        );

        assert_eq!(read_attempt, 1);
    }

    #[test]
    fn write_twice() {
        let mut write_attempt = 0;

        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, |content| {
                write_attempt += 1;
                match write_attempt {
                    1 => {
                        assert_eq!(&*content, b"Write one and two");
                        Ok(())
                    }
                    _ => panic!("Second write() call.  Content: '{:?}'", content),
                }
            }),
            |proxy| async move {
                assert_write!(proxy, "Write one");
                assert_write!(proxy, " and two");
                assert_close!(proxy);
            },
        );

        assert_eq!(write_attempt, 1);
    }

    #[test]
    fn read_error() {
        let mut read_attempt = 0;

        let server = read_only(|| {
            read_attempt += 1;
            match read_attempt {
                1 => Err(Status::SHOULD_WAIT),
                2 => Ok(b"Have value".to_vec()),
                _ => panic!("Third call to read()."),
            }
        });

        run_server_client_with_open_requests_channel(server, |mut open_sender| async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

            {
                let (proxy, server_end) =
                    create_proxy::<FileMarker>().expect("Failed to create connection endpoints");

                open_sender.send((flags, 0, server_end)).await.unwrap();
                assert_event!(proxy, FileEvent::OnOpen_ { s, info }, {
                    assert_eq!(Status::from_raw(s), Status::SHOULD_WAIT);
                    assert_eq!(info, None);
                });
            }

            {
                let (proxy, server_end) =
                    create_proxy::<FileMarker>().expect("Failed to create connection endpoints");

                open_sender.send((flags, 0, server_end)).await.unwrap();
                assert_event!(proxy, FileEvent::OnOpen_ { s, info }, {
                    assert_eq!(s, ZX_OK);
                    assert_eq!(
                        info,
                        Some(Box::new(NodeInfo::File(FileObject { event: None, stream: None })))
                    );
                });

                assert_read!(proxy, "Have value");
                assert_close!(proxy);
            }
        });
    }

    #[test]
    fn read_write_no_write_flag() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_write(
                || Ok(b"Can read".to_vec()),
                100,
                |_content| {
                    panic!("File was not opened as writable");
                },
            ),
            |proxy| async move {
                assert_read!(proxy, "Can read");
                assert_write_err!(proxy, "Can write", Status::ACCESS_DENIED);
                assert_write_at_err!(proxy, 0, "Can write", Status::ACCESS_DENIED);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_write_no_read_flag() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            read_write(
                || {
                    panic!("File was not opened as readable");
                },
                100,
                |content| {
                    assert_eq!(*&content, b"Can write");
                    Ok(())
                },
            ),
            |proxy| async move {
                assert_read_err!(proxy, Status::ACCESS_DENIED);
                assert_read_at_err!(proxy, 0, Status::ACCESS_DENIED);
                assert_write!(proxy, "Can write");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_write_ignore_posix_flag() {
        let mut write_attempt = 0;

        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_POSIX,
            read_write(
                || Ok(b"Content".to_vec()),
                100,
                |content| {
                    write_attempt += 1;
                    match write_attempt {
                        1 => {
                            assert_eq!(*&content, b"Can write");
                            Ok(())
                        }
                        _ => panic!("Second write() call.  Content: '{:?}'", content),
                    }
                },
            ),
            |proxy| async move {
                assert_read!(proxy, "Content");
                assert_seek!(proxy, 0, Start);
                assert_write!(proxy, "Can write");
                assert_seek!(proxy, 0, Start);
                assert_read!(proxy, "Can write");
                assert_close!(proxy);
            },
        );

        assert_eq!(write_attempt, 1);
    }

    #[test]
    /// When the read handler returns a value that is larger then the specified capacity of the
    /// file, write handler will receive it as is, uncut.  This behaviour is specified in the
    /// description of [`PseudoFile::capacity`].
    fn read_returns_more_than_capacity() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Read handler may return more than capacity".to_vec()),
                10,
                |content| {
                    assert_eq!(content, b"Write then could write beyond max capacity".to_vec());
                    Ok(())
                },
            ),
            |proxy| {
                async move {
                    assert_read!(proxy, "Read");
                    assert_seek!(proxy, 0, Start);
                    // Need to write something, otherwise write handler will not be called.
                    // " capacity" is a leftover from what read handler has returned.
                    assert_write!(proxy, "Write then could write beyond max");
                    assert_close!(proxy);
                }
            },
        );
    }

    #[test]
    fn write_error() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, |content| {
                assert_eq!(*&content, b"Wrong format");
                Err(Status::INVALID_ARGS)
            }),
            |proxy| async move {
                assert_write!(proxy, "Wrong");
                assert_write!(proxy, " format");
                assert_close_err!(proxy, Status::INVALID_ARGS);
            },
        );
    }

    #[test]
    fn write_and_drop_connection() {
        let (write_call_sender, write_call_receiver) = oneshot::channel::<()>();
        let mut write_call_sender = Some(write_call_sender);
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, |content| {
                assert_eq!(*&content, b"Updated content");
                write_call_sender.take().unwrap().send(()).unwrap();
                Ok(())
            }),
            |proxy| async move {
                assert_write!(proxy, "Updated content");
                drop(proxy);
                write_call_receiver.await.unwrap();
            },
        );
    }

    #[test]
    fn open_truncate() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_TRUNCATE,
            read_write(
                || panic!("OPEN_FLAG_TRUNCATE means read() is not called."),
                100,
                |content| {
                    assert_eq!(*&content, b"File content");
                    Ok(())
                },
            ),
            |proxy| async move {
                assert_write!(proxy, "File content");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_at_0() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only_static("Whole file content"),
            |proxy| async move {
                assert_read_at!(proxy, 0, "Whole file content");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_at_overlapping() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only_static("Content of the file"),
            //                0         1
            //                0123456789012345678
            |proxy| async move {
                assert_read_at!(proxy, 3, "tent of the");
                assert_read_at!(proxy, 11, "the file");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_mixed_with_read_at() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only_static("Content of the file"),
            //                0         1
            //                0123456789012345678
            |proxy| async move {
                assert_read!(proxy, "Content");
                assert_read_at!(proxy, 3, "tent of the");
                assert_read!(proxy, " of the ");
                assert_read_at!(proxy, 11, "the file");
                assert_read!(proxy, "file");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn write_at_0() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, |content| {
                assert_eq!(*&content, b"File content");
                Ok(())
            }),
            |proxy| async move {
                assert_write_at!(proxy, 0, "File content");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn write_at_overlapping() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, |content| {
                assert_eq!(*&content, b"Whole file content");
                //                      0         1
                //                      012345678901234567
                Ok(())
            }),
            |proxy| async move {
                assert_write_at!(proxy, 8, "le content");
                assert_write_at!(proxy, 6, "file");
                assert_write_at!(proxy, 0, "Whole file");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn write_mixed_with_write_at() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, |content| {
                assert_eq!(*&content, b"Whole file content");
                //                      0         1
                //                      012345678901234567
                Ok(())
            }),
            |proxy| async move {
                assert_write!(proxy, "whole");
                assert_write_at!(proxy, 0, "Who");
                assert_write!(proxy, " 1234 ");
                assert_write_at!(proxy, 6, "file");
                assert_write!(proxy, "content");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn seek_read_write() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Initial".to_vec()),
                100,
                |content| {
                    assert_eq!(*&content, b"Final content");
                    //                      0         1
                    //                      0123456789012
                    Ok(())
                },
            ),
            |proxy| {
                async move {
                    assert_read!(proxy, "Init");
                    assert_write!(proxy, "l con");
                    // buffer: "Initl con"
                    assert_seek!(proxy, 0, Start);
                    assert_write!(proxy, "Fina");
                    // buffer: "Final con"
                    assert_seek!(proxy, 0, End, 9);
                    assert_write!(proxy, "tent");
                    assert_close!(proxy);
                }
            },
        );
    }

    #[test]
    fn seek_valid_positions() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only_static("Long file content"),
            //                0         1
            //                01234567890123456
            |proxy| async move {
                assert_seek!(proxy, 5, Start);
                assert_read!(proxy, "file");
                assert_seek!(proxy, 1, Current, 10);
                assert_read!(proxy, "content");
                assert_seek!(proxy, -12, End, 5);
                assert_read!(proxy, "file content");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn seek_valid_after_size_before_capacity() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Content".to_vec()),
                //      0123456
                100,
                |content| {
                    assert_eq!(*&content, b"Content extended further");
                    //                      0         1         2
                    //                      012345678901234567890123
                    Ok(())
                },
            ),
            |proxy| {
                async move {
                    assert_seek!(proxy, 7, Start);
                    // POSIX wants this to be a zero read. fxbug.dev/33425.
                    assert_read!(proxy, "");
                    assert_write!(proxy, " ext");
                    //      "Content ext"));
                    assert_seek!(proxy, 3, Current, 14);
                    assert_write!(proxy, "ed");
                    //      "Content ext000ed"));
                    assert_seek!(proxy, 4, End, 20);
                    assert_write!(proxy, "ther");
                    //      "Content ext000ed0000ther"));
                    //       0         1         2
                    //       012345678901234567890123
                    assert_seek!(proxy, 11, Start);
                    assert_write!(proxy, "end");
                    assert_seek!(proxy, 16, Start);
                    assert_write!(proxy, " fur");
                    assert_close!(proxy);
                }
            },
        );
    }

    #[test]
    fn seek_invalid_before_0() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only_static("Seek position is unaffected"),
            //                0         1         2
            //                012345678901234567890123456
            |proxy| async move {
                assert_seek_err!(proxy, -10, Current, Status::OUT_OF_RANGE, 0);
                assert_read!(proxy, "Seek");
                assert_seek_err!(proxy, -10, Current, Status::OUT_OF_RANGE, 4);
                assert_read!(proxy, " position");
                assert_seek_err!(proxy, -100, End, Status::OUT_OF_RANGE, 13);
                assert_read!(proxy, " is unaffected");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn seek_invalid_after_capacity() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Content".to_vec()),
                //      0123456
                10,
                |_content| panic!("No writes should have happened"),
            ),
            |proxy| async move {
                assert_seek!(proxy, 8, Start);
                assert_seek_err!(proxy, 12, Start, Status::OUT_OF_RANGE, 8);
                assert_seek_err!(proxy, 3, Current, Status::OUT_OF_RANGE, 8);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn seek_after_truncate() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Content".to_vec()),
                //      0123456
                100,
                |content| {
                    assert_eq!(*&content, b"Content\0\0\0end");
                    //                      0            1
                    //                      01234567 8 9 012
                    Ok(())
                },
            ),
            |proxy| async move {
                assert_truncate!(proxy, 12);
                assert_seek!(proxy, 10, Start);
                assert_write!(proxy, "end");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    /// Make sure that even if the file content is larger than the capacity, seek does not allow to
    /// go beyond the maximum of the capacity and length.
    fn seek_beyond_capacity_in_large_file() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Long content".to_vec()),
                //      0         1
                //      012345678901
                8,
                |_content| panic!("No writes should have happened"),
            ),
            |proxy| async move {
                assert_seek!(proxy, 10, Start);
                assert_seek_err!(proxy, 12, Start, Status::OUT_OF_RANGE, 10);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn truncate_to_0() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Content".to_vec()),
                //      0123456
                100,
                |content| {
                    assert_eq!(*&content, b"Replaced");
                    Ok(())
                },
            ),
            |proxy| {
                async move {
                    assert_read!(proxy, "Content");
                    assert_truncate!(proxy, 0);
                    // truncate should not change the seek position.
                    assert_seek!(proxy, 0, Current, 7);
                    assert_seek!(proxy, 0, Start);
                    assert_write!(proxy, "Replaced");
                    assert_close!(proxy);
                }
            },
        );
    }

    #[test]
    fn write_then_truncate() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, |content| {
                assert_eq!(*&content, b"Replaced");
                Ok(())
            }),
            |proxy| async move {
                assert_write!(proxy, "Replaced content");
                assert_truncate!(proxy, 8);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn truncate_beyond_capacity() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(10, |_content| panic!("No writes should have happened")),
            |proxy| async move {
                assert_truncate_err!(proxy, 20, Status::OUT_OF_RANGE);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn truncate_read_only_file() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only_static("Read-only content"),
            |proxy| async move {
                assert_truncate_err!(proxy, 10, Status::ACCESS_DENIED);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    /// Make sure that when the read hander has returned a buffer that is larger than the capacity,
    /// we can cut it down to a something that is still larger then the capacity.  But we can not
    /// undo that cut.
    fn truncate_large_file_beyond_capacity() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Content is very long".to_vec()),
                //      0         1
                //      01234567890123456789
                10,
                |content| {
                    assert_eq!(*&content, b"Content is very");
                    //                      0         1
                    //                      012345678901234
                    Ok(())
                },
            ),
            |proxy| async move {
                assert_read!(proxy, "Content");
                assert_truncate_err!(proxy, 40, Status::OUT_OF_RANGE);
                assert_truncate!(proxy, 16);
                assert_truncate!(proxy, 14);
                assert_truncate_err!(proxy, 16, Status::OUT_OF_RANGE);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn clone_reduce_access() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Initial content".to_vec()),
                100,
                |content| {
                    assert_eq!(*&content, b"As updated");
                    Ok(())
                },
            ),
            |first_proxy| async move {
                assert_read!(first_proxy, "Initial content");
                assert_truncate!(first_proxy, 0);
                assert_seek!(first_proxy, 0, Start);
                assert_write!(first_proxy, "As updated");

                let second_proxy = clone_get_file_proxy_assert_ok!(
                    &first_proxy,
                    OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE
                );

                assert_read!(second_proxy, "Initial content");
                assert_truncate_err!(second_proxy, 0, Status::ACCESS_DENIED);
                assert_write_err!(second_proxy, "As updated", Status::ACCESS_DENIED);

                assert_close!(first_proxy);
                assert_close!(second_proxy);
            },
        );
    }

    #[test]
    fn clone_inherit_access() {
        use fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS;

        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Initial content".to_vec()),
                100,
                |content| {
                    assert_eq!(*&content, b"As updated");
                    Ok(())
                },
            ),
            |first_proxy| async move {
                assert_read!(first_proxy, "Initial content");
                assert_truncate!(first_proxy, 0);
                assert_seek!(first_proxy, 0, Start);
                assert_write!(first_proxy, "As updated");

                let second_proxy = clone_get_file_proxy_assert_ok!(
                    &first_proxy,
                    CLONE_FLAG_SAME_RIGHTS | OPEN_FLAG_DESCRIBE
                );

                assert_read!(second_proxy, "Initial content");
                assert_truncate!(second_proxy, 0);
                assert_seek!(second_proxy, 0, Start);
                assert_write!(second_proxy, "As updated");

                assert_close!(first_proxy);
                assert_close!(second_proxy);
            },
        );
    }

    #[test]
    fn get_attr_read_only() {
        run_server_client(OPEN_RIGHT_READABLE, read_only_static("Content"), |proxy| async move {
            assert_get_attr!(
                proxy,
                NodeAttributes {
                    mode: MODE_TYPE_FILE | S_IRUSR,
                    id: INO_UNKNOWN,
                    content_size: 0,
                    storage_size: 0,
                    link_count: 1,
                    creation_time: 0,
                    modification_time: 0,
                }
            );
            assert_close!(proxy);
        });
    }

    #[test]
    fn get_attr_write_only() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(10, |_content| panic!("No changes")),
            |proxy| async move {
                assert_get_attr!(
                    proxy,
                    NodeAttributes {
                        mode: MODE_TYPE_FILE | S_IWUSR,
                        id: INO_UNKNOWN,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 1,
                        creation_time: 0,
                        modification_time: 0,
                    }
                );
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn get_attr_read_write() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(|| Ok(b"Content".to_vec()), 10, |_content| panic!("No changes")),
            |proxy| async move {
                assert_get_attr!(
                    proxy,
                    NodeAttributes {
                        mode: MODE_TYPE_FILE | S_IWUSR | S_IRUSR,
                        id: INO_UNKNOWN,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 1,
                        creation_time: 0,
                        modification_time: 0,
                    }
                );
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn get_attr_read_only_attr() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only_attr(S_IXOTH | S_IROTH | S_IXGRP | S_IRGRP | S_IXUSR | S_IRUSR, || {
                Ok(b"Content".to_vec())
            }),
            |proxy| async move {
                assert_get_attr!(
                    proxy,
                    NodeAttributes {
                        mode: MODE_TYPE_FILE
                            | (S_IXOTH | S_IROTH | S_IXGRP | S_IRGRP | S_IXUSR | S_IRUSR),
                        id: INO_UNKNOWN,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 1,
                        creation_time: 0,
                        modification_time: 0,
                    }
                );
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn get_attr_write_only_attr() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only_attr(S_IWOTH | S_IWGRP | S_IWUSR, 10, |_content| panic!("No changes")),
            |proxy| async move {
                assert_get_attr!(
                    proxy,
                    NodeAttributes {
                        mode: MODE_TYPE_FILE | S_IWOTH | S_IWGRP | S_IWUSR,
                        id: INO_UNKNOWN,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 1,
                        creation_time: 0,
                        modification_time: 0,
                    }
                );
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn get_attr_read_write_attr() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write_attr(
                S_IXOTH
                    | S_IROTH
                    | S_IWOTH
                    | S_IXGRP
                    | S_IRGRP
                    | S_IWGRP
                    | S_IXUSR
                    | S_IRUSR
                    | S_IWUSR,
                || Ok(b"Content".to_vec()),
                10,
                |_content| panic!("No changes"),
            ),
            |proxy| async move {
                assert_get_attr!(
                    proxy,
                    NodeAttributes {
                        mode: MODE_TYPE_FILE
                            | (S_IXOTH
                                | S_IROTH
                                | S_IWOTH
                                | S_IXGRP
                                | S_IRGRP
                                | S_IWGRP
                                | S_IXUSR
                                | S_IRUSR
                                | S_IWUSR),
                        id: INO_UNKNOWN,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 1,
                        creation_time: 0,
                        modification_time: 0,
                    }
                );
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn clone_cannot_increase_access() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_write(
                || Ok(b"Initial content".to_vec()),
                100,
                |_content| {
                    panic!("Clone should not be able to write.");
                },
            ),
            |first_proxy| async move {
                assert_read!(first_proxy, "Initial content");
                assert_write_err!(first_proxy, "Write attempt", Status::ACCESS_DENIED);

                let second_proxy = clone_as_file_assert_err!(
                    &first_proxy,
                    OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE,
                    Status::ACCESS_DENIED
                );

                assert_read_fidl_err_closed!(second_proxy);
                assert_write_fidl_err_closed!(second_proxy, "Write attempt");
                assert_close!(first_proxy);
            },
        );
    }

    #[test]
    fn node_reference_ignores_read_access() {
        run_server_client(
            OPEN_FLAG_NODE_REFERENCE | OPEN_RIGHT_READABLE,
            read_only(|| panic!("Not supposed to read!")),
            |proxy| async move {
                assert_read_err!(proxy, Status::ACCESS_DENIED);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn node_reference_ignores_write_access() {
        run_server_client(
            OPEN_FLAG_NODE_REFERENCE | OPEN_RIGHT_WRITABLE,
            write_only(10, |_content| panic!("Not supposed to write!")),
            |proxy| async move {
                assert_write_err!(proxy, "Can write", Status::ACCESS_DENIED);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    /// This test checks a somewhat non-trivial case.  Two clients are connected to the same file,
    /// and we want to make sure that they get individual buffers.  The file content will be
    /// different every time a new buffer is created, as `on_read` returns a string with an
    /// invocation count in it.
    ///
    /// [`run_server_client_with_open_requests_channel_and_executor`] is used to control relative
    /// execution of the clients and the server.  Clients wait before they open the file, read the
    /// file content and then wait before reading the file content once again.
    ///
    /// `run_until_stalled` and `oneshot::channel` are used to make sure that the test execution
    /// does not have race conditions.  We check that the futures are still running and check the
    /// `on_read` invocation counter.  See `executor` argument of the
    /// `run_server_client_with_open_requests_channel_and_executor` invocation.
    fn mock_directory_with_one_file_and_two_connections() {
        // If futures::join would provide a way to "unpack" an incomplete (or complete) Join
        // future, this test could have been written a bit easier.  Without the unpack
        // functionality as soon as the pseudo_file is combined the client into a joined future,
        // there is no way to add new connections to it.  So I need to have a select! loop.  I do
        // not see a reason why Join should not allow to unpack, except that this functionality is
        // probably only useful in tests or some other very special situations, so it was probably
        // just not implemented yet.
        //
        // On the up side, this test implementation is half way of what a pseudo directory will
        // (maybe already does) look like - may catch more relevant bugs.
        //
        // I was thinking about something like this:
        //
        // let future = server.join(first_client);
        // pin_mut!(future);
        // if let Poll::Ready((Err(e), ())) = exec.run_until_stalled(&mut future) {
        //     panic!("Server failed: {:?}", e);
        // }
        //
        // let (server, first_client) = match future.unpack() {
        //     Join(MaybeDone::Future(server), MaybeDone::Future(first_client))
        //         => (server, first_client),
        //     Join(MaybeDone::Done(res), _) => panic!("Server has stopped: {:?}", res),
        //     Join(_, MaybeDone::Done(())) => panic!("First client has stopped"),
        //     _ => panic!("Unepxected join state"),
        // };
        //
        // let (second_client, second_client_server_end) = ...
        //
        // server.open(flags, 0, &mut iter::empty(), second_client_server_end).unwrap();
        //
        // let future = server.join3(first_client);
        // pin_mut!(future);
        // if let Poll::Ready((Err(e), (), ())) = exec.run_until_stalled(&mut future) {
        //     panic!("Server failed: {:?}", e);
        // }

        let exec = fasync::Executor::new().expect("Executor creation failed");

        let create_client = move |expected_content: &'static str| {
            let (proxy, server_end) =
                create_proxy::<FileMarker>().expect("Failed to create connection endpoints");

            let (start_sender, start_receiver) = oneshot::channel::<()>();
            let (read_and_close_sender, read_and_close_receiver) = oneshot::channel::<()>();

            (
                move |mut open_sender: mpsc::Sender<(u32, u32, ServerEnd<FileMarker>)>| async move {
                    start_receiver.await.unwrap();

                    open_sender.send((OPEN_RIGHT_READABLE, 0, server_end)).await.unwrap();

                    assert_read!(proxy, expected_content);

                    read_and_close_receiver.await.unwrap();

                    assert_seek!(proxy, 0, Start);
                    assert_read!(proxy, expected_content);
                    assert_close!(proxy);
                },
                || {
                    start_sender.send(()).unwrap();
                },
                || {
                    read_and_close_sender.send(()).unwrap();
                },
            )
        };

        let read_count = &AtomicUsize::new(0);
        let (get_client1, client1_start, client1_read_and_close) = create_client("Content 1");
        let (get_client2, client2_start, client2_read_and_close) = create_client("Content 2");

        run_server_client_with_open_requests_channel_and_executor(
            exec,
            read_only_str(|| {
                let count = read_count.fetch_add(1, Ordering::Relaxed) + 1;
                Ok(format!("Content {}", count))
            }),
            |open_sender| async move {
                let client1 = get_client1(open_sender.clone());
                let client2 = get_client2(open_sender.clone());

                join(client1, client2).await;
            },
            |run_until_stalled_assert| {
                let mut run_and_check_read_count = |expected_count, should_complete: bool| {
                    run_until_stalled_assert(should_complete);
                    assert_eq!(read_count.load(Ordering::Relaxed), expected_count);
                };

                run_and_check_read_count(0, false);

                client1_start();

                run_and_check_read_count(1, false);

                client2_start();

                run_and_check_read_count(2, false);

                client1_read_and_close();

                run_and_check_read_count(2, false);

                client2_read_and_close();

                run_and_check_read_count(2, true)
            },
        );
    }
}
