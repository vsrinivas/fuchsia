// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of an asynchronous pseudo file with buffered connections backed by asynchronous
//! read and/or write callbacks.
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
//! Each connection to the pseudo file has a unique buffer that all operations through that
//! connection are applied to. This buffer is not synced with the underlying file while the
//! connection is open. If another connection closes and writes new contents to the file, the
//! buffers of the other connections are not updated to reflect it.

#![warn(missing_docs)]

use crate::{
    common::{inherit_rights_for_clone, send_on_open_with_error},
    directory::entry::{DirectoryEntry, EntryInfo},
    file::{
        connection::{
            BufferResult, ConnectionState, FileConnection, FileConnectionFuture,
            InitialConnectionState,
        },
        DEFAULT_READ_ONLY_PROTECTION_ATTRIBUTES, DEFAULT_READ_WRITE_PROTECTION_ATTRIBUTES,
        DEFAULT_WRITE_ONLY_PROTECTION_ATTRIBUTES,
    },
};

use {
    anyhow::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        FileCloseResponder, FileRequest, NodeMarker, DIRENT_TYPE_FILE, INO_UNKNOWN,
        MODE_PROTECTION_MASK, OPEN_FLAG_TRUNCATE, OPEN_RIGHT_READABLE,
    },
    fuchsia_zircon::Status,
    futures::{
        future::{Fuse, FusedFuture, FutureObj},
        stream::{FuturesUnordered, StreamExt, StreamFuture},
        FutureExt,
    },
    pin_utils::{unsafe_pinned, unsafe_unpinned},
    std::{
        future::Future,
        marker::Unpin,
        pin::Pin,
        task::{Context, Poll},
    },
    void::Void,
};

/// Creates a new read-only `AsyncPseudoFile` backed by the specified read handler.
///
/// The handler is called every time a new connection is added to the pseudo file. The contents are
/// placed in a connection-specific buffer. For more details on this interaction, see the module
/// documentation.
pub fn read_only<OnRead, OnReadRes>(
    on_read: OnRead,
) -> AsyncPseudoFile<
    OnRead,
    OnReadRes,
    fn(Vec<u8>) -> Fuse<FutureObj<'static, Result<(), Status>>>,
    Fuse<FutureObj<'static, Result<(), Status>>>,
>
where
    OnRead: FnMut() -> OnReadRes + Send,
    OnReadRes: Future<Output = Result<Vec<u8>, Status>> + Send,
{
    read_only_attr(DEFAULT_READ_ONLY_PROTECTION_ATTRIBUTES, on_read)
}

/// Same as [`read_only()`] but also allows to select custom attributes for the POSIX emulation
/// layer.  Note that only the MODE_PROTECTION_MASK part of the protection_attributes argument will
/// be stored.
pub fn read_only_attr<OnRead, OnReadRes>(
    protection_attributes: u32,
    on_read: OnRead,
) -> AsyncPseudoFile<
    OnRead,
    OnReadRes,
    fn(Vec<u8>) -> Fuse<FutureObj<'static, Result<(), Status>>>,
    Fuse<FutureObj<'static, Result<(), Status>>>,
>
where
    OnRead: FnMut() -> OnReadRes + Send,
    OnReadRes: Future<Output = Result<Vec<u8>, Status>> + Send,
{
    AsyncPseudoFile::new(protection_attributes & MODE_PROTECTION_MASK, Some(on_read), 0, None)
}

/// Creates a new write-only `AsyncPseudoFile` backed by the specified write handler.
///
/// The handler is called every time a connection to the pseudo file is closed with the contents of
/// the connection-specific buffer. For more details on this interaction, see the module
/// documentation.
pub fn write_only<OnWrite, OnWriteRes>(
    capacity: u64,
    on_write: OnWrite,
) -> AsyncPseudoFile<
    fn() -> Fuse<FutureObj<'static, Result<Vec<u8>, Status>>>,
    Fuse<FutureObj<'static, Result<Vec<u8>, Status>>>,
    OnWrite,
    OnWriteRes,
>
where
    OnWrite: FnMut(Vec<u8>) -> OnWriteRes + Send,
    OnWriteRes: Future<Output = Result<(), Status>> + Send,
{
    write_only_attr(DEFAULT_WRITE_ONLY_PROTECTION_ATTRIBUTES, capacity, on_write)
}

/// Same as [`write_only()`] but also allows to select custom attributes for the POSIX emulation
/// layer.  Note that only the MODE_PROTECTION_MASK part of the protection_attributes argument will
/// be stored.
pub fn write_only_attr<OnWrite, OnWriteRes>(
    protection_attributes: u32,
    capacity: u64,
    on_write: OnWrite,
) -> AsyncPseudoFile<
    fn() -> Fuse<FutureObj<'static, Result<Vec<u8>, Status>>>,
    Fuse<FutureObj<'static, Result<Vec<u8>, Status>>>,
    OnWrite,
    OnWriteRes,
>
where
    OnWrite: FnMut(Vec<u8>) -> OnWriteRes + Send,
    OnWriteRes: Future<Output = Result<(), Status>> + Send,
{
    AsyncPseudoFile::new(
        protection_attributes & MODE_PROTECTION_MASK,
        None,
        capacity,
        Some(on_write),
    )
}

/// Creates new `AsyncPseudoFile` backed by the specified read and write handlers.
///
/// The read handler is called every time a new connection is added to the pseudo file. The contents
/// are placed in a connection-specific buffer.
///
/// The write handler is called every time a connection to the pseudo file is closed with the
/// contents of the connection-specific buffer.
///
/// For more details on these interaction, see the module documentation.
pub fn read_write<OnRead, OnReadRes, OnWrite, OnWriteRes>(
    on_read: OnRead,
    capacity: u64,
    on_write: OnWrite,
) -> AsyncPseudoFile<OnRead, OnReadRes, OnWrite, OnWriteRes>
where
    OnRead: FnMut() -> OnReadRes + Send,
    OnReadRes: Future<Output = Result<Vec<u8>, Status>> + Send,
    OnWrite: FnMut(Vec<u8>) -> OnWriteRes + Send,
    OnWriteRes: Future<Output = Result<(), Status>> + Send,
{
    read_write_attr(DEFAULT_READ_WRITE_PROTECTION_ATTRIBUTES, on_read, capacity, on_write)
}

/// Same as [`read_write()`] but also allows to select custom attributes for the POSIX emulation
/// layer.  Note that only the MODE_PROTECTION_MASK part of the protection_attributes argument will
/// be stored.
pub fn read_write_attr<OnRead, OnReadRes, OnWrite, OnWriteRes>(
    protection_attributes: u32,
    on_read: OnRead,
    capacity: u64,
    on_write: OnWrite,
) -> AsyncPseudoFile<OnRead, OnReadRes, OnWrite, OnWriteRes>
where
    OnRead: FnMut() -> OnReadRes + Send,
    OnReadRes: Future<Output = Result<Vec<u8>, Status>> + Send,
    OnWrite: FnMut(Vec<u8>) -> OnWriteRes + Send,
    OnWriteRes: Future<Output = Result<(), Status>> + Send,
{
    AsyncPseudoFile::new(
        protection_attributes & MODE_PROTECTION_MASK,
        Some(on_read),
        capacity,
        Some(on_write),
    )
}

struct OnWriteFuture<OnWriteRes>
where
    OnWriteRes: Future<Output = Result<(), Status>> + Send,
{
    res: OnWriteRes,
    responder: Option<FileCloseResponder>,
}

impl<OnWriteRes> OnWriteFuture<OnWriteRes>
where
    OnWriteRes: Future<Output = Result<(), Status>> + Send,
{
    // unsafe: `OnWriteFuture` does not implement `Drop`, or `Unpin`.  And it is not
    // `#[repr(packed)]`.
    unsafe_pinned!(res: OnWriteRes);
    // unsafe: `responder` is not referenced by any other field in the `OnWriteFuture`, so it is
    // safe to get a mutable reference from a pinned one.
    unsafe_unpinned!(responder: Option<FileCloseResponder>);

    pub fn new(res: OnWriteRes, responder: Option<FileCloseResponder>) -> Self {
        OnWriteFuture { res, responder }
    }
}

impl<OnWriteRes> Future for OnWriteFuture<OnWriteRes>
where
    OnWriteRes: Future<Output = Result<(), Status>> + Send,
{
    type Output = ();

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        match self.as_mut().res().poll_unpin(cx) {
            Poll::Ready(Ok(())) => {
                if let Some(responder) = self.as_mut().responder().take() {
                    // If the responder fails here we have no recourse.
                    let _ = responder.send(Status::OK.into_raw());
                }
                Poll::Ready(())
            }
            Poll::Ready(Err(e)) => {
                if let Some(responder) = self.as_mut().responder().take() {
                    // If the responder fails here we have no recourse.
                    let _ = responder.send(e.into_raw());
                }
                Poll::Ready(())
            }
            Poll::Pending => Poll::Pending,
        }
    }
}

/// Return type for AsyncPseudoFile::handle_request().
struct HandleRequestResult {
    /// Current connection state.
    connection_state: ConnectionState,

    /// If the command might have added a new connection to this file, in which case we need to
    /// make sure to run `poll` method for the connections list again.
    added_new_connection: bool,
}

/// Implementation of an asychronous pseudo file in a virtual filesystem. This is created by passing
/// on_read and/or on_write callbacks to the exported constructor functions. These callbacks return
/// futures, unlike the simple pseudo file, where the callbacks are synchronous. See the module
/// documentation for more details.
pub struct AsyncPseudoFile<OnRead, OnReadRes, OnWrite, OnWriteRes>
where
    OnRead: FnMut() -> OnReadRes + Send,
    OnReadRes: Future<Output = Result<Vec<u8>, Status>> + Send,
    OnWrite: FnMut(Vec<u8>) -> OnWriteRes + Send,
    OnWriteRes: Future<Output = Result<(), Status>> + Send,
{
    protection_attributes: u32,
    on_read: Option<OnRead>,
    capacity: u64,
    on_write: Option<OnWrite>,
    connections: FuturesUnordered<StreamFuture<FileConnection>>,
    connection_futures: FuturesUnordered<FileConnectionFuture<OnReadRes>>,
    on_write_futures: FuturesUnordered<OnWriteFuture<OnWriteRes>>,
}

impl<OnRead, OnReadRes, OnWrite, OnWriteRes> AsyncPseudoFile<OnRead, OnReadRes, OnWrite, OnWriteRes>
where
    OnRead: FnMut() -> OnReadRes + Send,
    OnReadRes: Future<Output = Result<Vec<u8>, Status>> + Send,
    OnWrite: FnMut(Vec<u8>) -> OnWriteRes + Send,
    OnWriteRes: Future<Output = Result<(), Status>> + Send,
{
    fn new(
        protection_attributes: u32,
        on_read: Option<OnRead>,
        capacity: u64,
        on_write: Option<OnWrite>,
    ) -> Self {
        AsyncPseudoFile {
            protection_attributes,
            on_read,
            capacity,
            on_write,
            connections: FuturesUnordered::new(),
            connection_futures: FuturesUnordered::new(),
            on_write_futures: FuturesUnordered::new(),
        }
    }

    fn add_connection(&mut self, flags: u32, mode: u32, server_end: ServerEnd<NodeMarker>) {
        match FileConnection::connect_async(
            flags,
            mode,
            self.protection_attributes,
            server_end,
            self.on_read.is_some(),
            self.on_write.is_some(),
            self.capacity,
            |flags| self.init_buffer(flags),
        ) {
            InitialConnectionState::Failed => (),
            InitialConnectionState::Pending(fut) => self.connection_futures.push(fut),
            InitialConnectionState::Ready(conn) => self.connections.push(conn),
        }
    }

    fn init_buffer(&mut self, flags: u32) -> (BufferResult<OnReadRes>, bool) {
        if flags & OPEN_RIGHT_READABLE == 0 {
            (BufferResult::Empty, false)
        } else {
            match &mut self.on_read {
                None => (BufferResult::Empty, false),
                Some(on_read) => {
                    if flags & OPEN_FLAG_TRUNCATE != 0 {
                        (BufferResult::Empty, true)
                    } else {
                        (BufferResult::Future(on_read()), false)
                    }
                }
            }
        }
    }

    fn handle_request(
        &mut self,
        req: FileRequest,
        connection: &mut FileConnection,
    ) -> Result<HandleRequestResult, Error> {
        match req {
            FileRequest::Clone { flags, object, control_handle: _ } => {
                self.handle_clone(connection.flags, flags, object);
                Ok(HandleRequestResult {
                    connection_state: ConnectionState::Alive,
                    added_new_connection: true,
                })
            }
            FileRequest::Close { responder } => {
                self.handle_close(connection, Some(responder))?;
                Ok(HandleRequestResult {
                    connection_state: ConnectionState::Closed,
                    added_new_connection: false,
                })
            }
            _ => {
                connection.handle_request(req)?;
                Ok(HandleRequestResult {
                    connection_state: ConnectionState::Alive,
                    added_new_connection: false,
                })
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

    fn handle_close(
        &mut self,
        connection: &mut FileConnection,
        responder: Option<FileCloseResponder>,
    ) -> Result<(), fidl::Error> {
        if let Some(on_write) = &mut self.on_write {
            if let Some(fut) = connection.handle_close(|buf| Some(on_write(buf)), None) {
                self.on_write_futures.push(OnWriteFuture::new(fut, responder));
                return Ok(());
            }
        }

        if let Some(responder) = responder {
            responder.send(Status::OK.into_raw())
        } else {
            Ok(())
        }
    }
}

impl<OnRead, OnReadRes, OnWrite, OnWriteRes> DirectoryEntry
    for AsyncPseudoFile<OnRead, OnReadRes, OnWrite, OnWriteRes>
where
    OnRead: FnMut() -> OnReadRes + Send,
    OnReadRes: Future<Output = Result<Vec<u8>, Status>> + Send,
    OnWrite: FnMut(Vec<u8>) -> OnWriteRes + Send,
    OnWriteRes: Future<Output = Result<(), Status>> + Send,
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

impl<OnRead, OnReadRes, OnWrite, OnWriteRes> Unpin
    for AsyncPseudoFile<OnRead, OnReadRes, OnWrite, OnWriteRes>
where
    OnRead: FnMut() -> OnReadRes + Send,
    OnReadRes: Future<Output = Result<Vec<u8>, Status>> + Send,
    OnWrite: FnMut(Vec<u8>) -> OnWriteRes + Send,
    OnWriteRes: Future<Output = Result<(), Status>> + Send,
{
}

impl<OnRead, OnReadRes, OnWrite, OnWriteRes> FusedFuture
    for AsyncPseudoFile<OnRead, OnReadRes, OnWrite, OnWriteRes>
where
    OnRead: FnMut() -> OnReadRes + Send,
    OnReadRes: Future<Output = Result<Vec<u8>, Status>> + Send,
    OnWrite: FnMut(Vec<u8>) -> OnWriteRes + Send,
    OnWriteRes: Future<Output = Result<(), Status>> + Send,
{
    fn is_terminated(&self) -> bool {
        // The `AsyncPseudoFile` never completes, but once there are no more connections, it is
        // blocked until more connections are added. If the object currently polling an
        // `AsyncPseudoFile` with an empty set of connections is blocked on the `AsyncPseudoFile`
        // completing, it will never terminate.
        self.connections.len() == 0
            && self.connection_futures.len() == 0
            && self.on_write_futures.len() == 0
    }
}

impl<OnRead, OnReadRes, OnWrite, OnWriteRes> Future
    for AsyncPseudoFile<OnRead, OnReadRes, OnWrite, OnWriteRes>
where
    OnRead: FnMut() -> OnReadRes + Send,
    OnReadRes: Future<Output = Result<Vec<u8>, Status>> + Send,
    OnWrite: FnMut(Vec<u8>) -> OnWriteRes + Send,
    OnWriteRes: Future<Output = Result<(), Status>> + Send,
{
    type Output = Void;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        // NOTE See `crate::directory::Simple::poll` for the discussion on why we need a loop here,
        // as well as on the ordering.
        //
        // We go through all of our lists of futures, exausting all possible work that can be done
        // immediately. Also, according to the docs of FuturesUnordered, we need to call
        // `poll_next` when new futures are added in order to begin recieving wakeup calls for
        // them, and also to exhaust any work we can do immediately.
        //
        // Among 3 nested loops, for operations in each loop, all operations except one will
        // either:
        //   1. Push a future onto the next set of futures to execute.
        //   2. Put it back on it's own set of futures to execute.
        //   3. Drop the future altogether.
        //
        // The only operation that doesn't follow that order is `Clone`, which may place a future
        // into `connection_futures` list, while we are executing futures in `connections` list.
        // `handle_request` will let us know if that have happened, and we need to run the whole
        // loop again.
        loop {
            let mut rerun_connections = false;

            loop {
                match self.connection_futures.poll_next_unpin(cx) {
                    Poll::Ready(Some(Some(conn))) => self.connections.push(conn),
                    Poll::Ready(Some(None)) => (),
                    Poll::Ready(None) | Poll::Pending => break,
                }
            }

            loop {
                match self.connections.poll_next_unpin(cx) {
                    Poll::Ready(Some((maybe_request, mut connection))) => {
                        let state = match maybe_request {
                            Some(Ok(request)) => self
                                .handle_request(request, &mut connection)
                                // If an error occurred while processing a request we will just
                                // close the connection, effectively closing the underlying channel
                                // in the destructor.  Make one last attempt to call `handle_close`
                                // in case we have any modifications in the connection buffer.
                                .unwrap_or(HandleRequestResult {
                                    connection_state: ConnectionState::Dropped,
                                    added_new_connection: false,
                                }),
                            Some(Err(_)) | None => {
                                // If the connection was closed by the peer (`None`) or an error
                                // has occurred (`Some(Err)`), we still need to make sure we run
                                // the `on_write` handler.  There is nowhere to report any errors
                                // to, so we just ignore those.
                                HandleRequestResult {
                                    connection_state: ConnectionState::Dropped,
                                    added_new_connection: false,
                                }
                            }
                        };

                        match state {
                            HandleRequestResult {
                                connection_state: ConnectionState::Alive,
                                added_new_connection,
                            } => {
                                rerun_connections |= added_new_connection;
                                self.connections.push(connection.into_future())
                            }
                            HandleRequestResult {
                                connection_state: ConnectionState::Closed,
                                added_new_connection,
                            } => rerun_connections |= added_new_connection,
                            HandleRequestResult {
                                connection_state: ConnectionState::Dropped,
                                added_new_connection,
                            } => {
                                rerun_connections |= added_new_connection;
                                let _ = self.handle_close(&mut connection, None);
                            }
                        }
                    }
                    Poll::Ready(None) | Poll::Pending => break,
                }
            }

            loop {
                match self.on_write_futures.poll_next_unpin(cx) {
                    Poll::Ready(Some(())) => (),
                    Poll::Ready(None) | Poll::Pending => break,
                }
            }

            if !rerun_connections {
                break;
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
            run_server_client, run_server_client_with_executor,
            run_server_client_with_open_requests_channel,
            run_server_client_with_open_requests_channel_and_executor,
        },
        fidl::endpoints::create_proxy,
        fidl_fuchsia_io::{
            FileEvent, FileMarker, FileObject, NodeInfo, OPEN_FLAG_DESCRIBE,
            OPEN_FLAG_NODE_REFERENCE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
        },
        fuchsia_async as fasync,
        fuchsia_zircon::{sys::ZX_OK, Status},
        futures::{
            channel::{mpsc, oneshot},
            future::{join, lazy},
            FutureExt, SinkExt,
        },
        std::sync::{
            atomic::{AtomicUsize, Ordering},
            Arc,
        },
    };

    macro_rules! fast_future {
        ( $x:expr ) => {
            lazy(move |_| $x)
        };
    }

    #[test]
    fn read_only_read() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(|| fast_future!(Ok(b"Read only test".to_vec()))),
            |proxy| async move {
                assert_read!(proxy, "Read only test");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_only_read_no_status() {
        let exec = fasync::Executor::new().expect("Executor creation failed");
        let (check_event_send, check_event_recv) = oneshot::channel::<()>();

        run_server_client_with_open_requests_channel_and_executor(
            exec,
            read_only(|| fast_future!(Ok(b"Read only test".to_vec()))),
            |mut open_sender| {
                async move {
                    let (proxy, server_end) = create_proxy::<FileMarker>()
                        .expect("Failed to create connection endpoints");

                    let flags = OPEN_RIGHT_READABLE;
                    open_sender.send((flags, 0, server_end)).await.unwrap();
                    check_event_recv.await.unwrap();
                    assert_no_event!(proxy);
                    // NOTE: logic added after `assert_no_event!` will not currently be run. this test
                    // will need to be updated after fxbug.dev/33709 is completed.
                }
            },
            |run_until_stalled_assert| {
                run_until_stalled_assert(false);
                check_event_send.send(()).unwrap();
                run_until_stalled_assert(false);
            },
        );
    }

    #[test]
    fn read_only_read_with_describe() {
        run_server_client_with_open_requests_channel(
            read_only(|| fast_future!(Ok(b"Read only test".to_vec()))),
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
    fn write_only_write() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, |content| {
                fast_future!({
                    assert_eq!(&*content, b"Write only test");
                    Ok(())
                })
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
                || fast_future!(Ok(b"Hello".to_vec())),
                100,
                |content| {
                    fast_future!({
                        assert_eq!(*&content, b"Hello, world!");
                        Ok(())
                    })
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
        let attempts = Arc::new(AtomicUsize::new(0));

        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(|| {
                let attempts = attempts.clone();
                lazy(move |_| {
                    let read_attempt = attempts.fetch_add(1, Ordering::Relaxed);
                    match read_attempt {
                        0 => Ok(b"State one".to_vec()),
                        _ => panic!("Called on_read() a second time."),
                    }
                })
            }),
            |proxy| async move {
                assert_read!(proxy, "State one");
                assert_seek!(proxy, 0, Start);
                assert_read!(proxy, "State one");
                assert_close!(proxy);
            },
        );

        assert_eq!(attempts.load(Ordering::Relaxed), 1);
    }

    #[test]
    fn write_twice() {
        let attempts = Arc::new(AtomicUsize::new(0));

        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, |content| {
                let attempts = attempts.clone();
                lazy(move |_| {
                    let write_attempt = attempts.fetch_add(1, Ordering::Relaxed);
                    match write_attempt {
                        0 => {
                            assert_eq!(&*content, b"Write one and two");
                            Ok(())
                        }
                        _ => panic!("Second write() call.  Content: '{:?}'", content),
                    }
                })
            }),
            |proxy| async move {
                assert_write!(proxy, "Write one");
                assert_write!(proxy, " and two");
                assert_close!(proxy);
            },
        );

        assert_eq!(attempts.load(Ordering::Relaxed), 1);
    }

    #[test]
    fn read_error() {
        let read_attempt = Arc::new(AtomicUsize::new(0));

        let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
        let server = read_only(|| {
            let read_attempt = read_attempt.clone();
            fast_future!({
                let attempt = read_attempt.fetch_add(1, Ordering::Relaxed);
                match attempt {
                    0 => Err(Status::SHOULD_WAIT),
                    1 => Ok(b"Have value".to_vec()),
                    _ => panic!("Third call to read()."),
                }
            })
        });

        run_server_client_with_open_requests_channel(server, |mut open_sender| async move {
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
                || fast_future!(Ok(b"Can read".to_vec())),
                100,
                |_content| {
                    fast_future!({
                        panic!("File was not opened as writable");
                    })
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
                    fast_future!({
                        panic!("File was not opened as readable");
                    })
                },
                100,
                |content| {
                    fast_future!({
                        assert_eq!(*&content, b"Can write");
                        Ok(())
                    })
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
    /// When the read handler returns a value that is larger then the specified capacity of the
    /// file, write handler will receive it as is, uncut.  This behaviour is specified in the
    /// description of [`PseudoFileImpl::capacity`].
    fn read_returns_more_than_capacity() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || fast_future!(Ok(b"Read handler may return more than capacity".to_vec())),
                10,
                |content| {
                    fast_future!({
                        assert_eq!(content, b"Write then could write beyond max capacity".to_vec());
                        Ok(())
                    })
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
                fast_future!({
                    assert_eq!(*&content, b"Wrong format");
                    Err(Status::INVALID_ARGS)
                })
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
            write_only(100, move |content| {
                let write_call_sender = write_call_sender.take().unwrap();
                async move {
                    assert_eq!(*&content, b"Updated content");
                    write_call_sender.send(()).unwrap();
                    Ok(())
                }
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
                || fast_future!(panic!("OPEN_FLAG_TRUNCATE means read() is not called.")),
                100,
                |content| {
                    fast_future!({
                        assert_eq!(*&content, b"File content");
                        Ok(())
                    })
                },
            ),
            |proxy| async move {
                assert_write!(proxy, "File content");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn clone_reduce_access() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || fast_future!(Ok(b"Initial content".to_vec())),
                100,
                |content| {
                    fast_future!({
                        assert_eq!(*&content, b"As updated");
                        Ok(())
                    })
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
    fn clone_cannot_increase_access() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_write(
                || fast_future!(Ok(b"Initial content".to_vec())),
                100,
                |_content| {
                    fast_future!({
                        panic!("Clone should not be able to write.");
                    })
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
            read_only(|| fast_future!(panic!("Not supposed to read!"))),
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
            write_only(100, |_content| fast_future!(panic!("Not supposed to write!"))),
            |proxy| async move {
                assert_write_err!(proxy, "Can write", Status::ACCESS_DENIED);
                assert_close!(proxy);
            },
        );
    }

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
    #[test]
    fn mock_directory_with_one_file_and_two_connections() {
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

        let read_count = Arc::new(AtomicUsize::new(0));
        let (get_client1, client1_start, client1_read_and_close) = create_client("Content 0");
        let (get_client2, client2_start, client2_read_and_close) = create_client("Content 1");

        run_server_client_with_open_requests_channel_and_executor(
            exec,
            read_only(|| {
                let read_count = read_count.clone();
                fast_future!({
                    let count = read_count.fetch_add(1, Ordering::Relaxed);
                    Ok(format!("Content {}", count).into_bytes())
                })
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

    #[test]
    fn slow_on_read() {
        // The rest of the tests for the async file all use `fast_future!` to wrap their relevant
        // pseudo file callbacks in the simplest possible way, using the lazy future combinator.
        // However, we want to confirm that we behave as expected if we have an on_read future that
        // doesn't immediately return.
        //
        // This test creates an on_read future that doesn't return `Ready` with the result of the
        // on_read operation until it receives a signal on a oneshot channel. We confirm the
        // behavior we expect from the file - notably that we are able to send multiple requests to
        // the file before the connection is actually created and populated, and have them be
        // executed once the buffer is filled with what we expect.
        let exec = fasync::Executor::new().expect("Executor creation failed");

        let read_counter = Arc::new(AtomicUsize::new(0));
        let client_counter = Arc::new(AtomicUsize::new(0));
        let client_count = client_counter.clone();
        let (finish_future_sender, finish_future_receiver) = oneshot::channel::<()>();
        let finish_future_receiver = finish_future_receiver.shared();

        run_server_client_with_executor(
            OPEN_RIGHT_READABLE,
            exec,
            read_only(|| {
                let read_counter = read_counter.clone();
                let finish_future_receiver = finish_future_receiver.clone();
                async move {
                    read_counter.fetch_add(1, Ordering::Relaxed);
                    finish_future_receiver
                        .await
                        .expect("finish_future_sender was not called before been dropped.");
                    read_counter.fetch_add(1, Ordering::Relaxed);
                    Ok(b"content".to_vec())
                }
            }),
            |proxy| async move {
                client_count.fetch_add(1, Ordering::Relaxed);

                assert_read!(proxy, "content");

                assert_seek!(proxy, 4, Start);
                assert_read!(proxy, "ent");
                assert_close!(proxy);

                client_count.fetch_add(1, Ordering::Relaxed);
            },
            |run_until_stalled_assert| {
                let check_read_client_counts = |expected_read, expected_client| {
                    assert_eq!(read_counter.load(Ordering::Relaxed), expected_read);
                    assert_eq!(client_counter.load(Ordering::Relaxed), expected_client);
                };
                run_until_stalled_assert(false);

                // on_read is waiting yet, as well as the client.
                check_read_client_counts(1, 1);

                finish_future_sender.send(()).unwrap();
                run_until_stalled_assert(true);

                // Both have reached the end.
                check_read_client_counts(2, 2);
            },
        );
    }

    #[test]
    fn slow_on_write() {
        // this test is pretty similar to the above, except that it lags the on_write call instead.
        let exec = fasync::Executor::new().expect("Executor creation failed");

        let write_counter = Arc::new(AtomicUsize::new(0));
        let client_counter = Arc::new(AtomicUsize::new(0));
        let client_count = client_counter.clone();
        let (finish_future_sender, finish_future_receiver) = oneshot::channel::<()>();
        let finish_future_receiver = finish_future_receiver.shared();

        run_server_client_with_executor(
            OPEN_RIGHT_WRITABLE,
            exec,
            write_only(100, |content| {
                let write_counter = write_counter.clone();
                let finish_future_receiver = finish_future_receiver.clone();
                async move {
                    assert_eq!(*&content, b"content");
                    write_counter.fetch_add(1, Ordering::Relaxed);
                    finish_future_receiver
                        .await
                        .expect("finish_future_sender was not called before been dropped.");
                    write_counter.fetch_add(1, Ordering::Relaxed);
                    Ok(())
                }
            }),
            |proxy| async move {
                client_count.fetch_add(1, Ordering::Relaxed);

                assert_write!(proxy, "content");
                assert_close!(proxy);

                client_count.fetch_add(1, Ordering::Relaxed);
            },
            |run_until_stalled_assert| {
                let check_write_client_counts = |expected_write, expected_client| {
                    assert_eq!(write_counter.load(Ordering::Relaxed), expected_write);
                    assert_eq!(client_counter.load(Ordering::Relaxed), expected_client);
                };

                run_until_stalled_assert(false);

                // The server and the client are waiting.
                check_write_client_counts(1, 1);

                finish_future_sender.send(()).unwrap();
                run_until_stalled_assert(true);

                // The server and the client are done.
                check_write_client_counts(2, 2);
            },
        );
    }
}
