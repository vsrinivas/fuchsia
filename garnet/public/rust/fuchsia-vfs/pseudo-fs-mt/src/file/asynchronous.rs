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
    file::connection::{AsyncInitBuffer, AsyncUpdate, FileConnection, FileWithPerConnectionBuffer},
    path::Path,
};

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{NodeMarker, DIRENT_TYPE_FILE, INO_UNKNOWN},
    fuchsia_zircon::Status,
    futures::{task::Context, Future, Poll},
    std::{pin::Pin, sync::Arc},
};

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
}

#[cfg(test)]
mod tests {
    use super::{read_only, read_write, write_only};

    // Macros are exported into the root of the crate.
    use crate::{
        assert_close, assert_close_err, assert_event, assert_get_attr, assert_no_event,
        assert_read, assert_read_at, assert_read_at_err, assert_read_err, assert_read_fidl_err,
        assert_seek, assert_seek_err, assert_truncate, assert_truncate_err, assert_write,
        assert_write_at, assert_write_at_err, assert_write_err, assert_write_fidl_err,
        clone_as_file_assert_err, clone_get_file_proxy_assert_ok, clone_get_proxy_assert,
    };

    use crate::{
        directory::entry::DirectoryEntry,
        execution_scope::ExecutionScope,
        file::test_utils::{
            run_client, run_client_with_executor, run_server_client,
            run_server_client_with_executor,
        },
        path::Path,
    };

    use {
        fidl::endpoints::create_proxy,
        fidl_fuchsia_io::{
            FileMarker, NodeAttributes, INO_UNKNOWN, MODE_TYPE_FILE, OPEN_FLAG_DESCRIBE,
            OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_POSIX, OPEN_FLAG_TRUNCATE, OPEN_RIGHT_READABLE,
            OPEN_RIGHT_WRITABLE,
        },
        fuchsia_async::Executor,
        fuchsia_zircon::{sys::ZX_OK, Status},
        futures::{channel::oneshot, future::join, FutureExt},
        libc::{S_IRUSR, S_IWUSR},
        std::sync::{
            atomic::{AtomicUsize, Ordering},
            Arc,
        },
    };

    #[test]
    fn read_only_read() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(async || Ok(b"Read only test".to_vec())),
            async move |proxy| {
                assert_read!(proxy, "Read only test");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_only_ignore_posix_flag() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_FLAG_POSIX,
            read_write(
                async || Ok(b"Content".to_vec()),
                100,
                async move |_content| {
                    panic!("OPEN_FLAG_POSIX should be ignored for files");
                },
            ),
            async move |proxy| {
                assert_read!(proxy, "Content");
                assert_write_err!(proxy, "Can write", Status::ACCESS_DENIED);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_only_read_no_status() {
        let exec = Executor::new().expect("Executor creation failed");
        let (check_event_send, check_event_recv) = oneshot::channel::<()>();

        run_server_client_with_executor(
            OPEN_RIGHT_READABLE,
            exec,
            read_only(async || Ok(b"Read only test".to_vec())),
            async move |proxy| {
                // Make sure `open()` call is complete, before we start checking.
                await!(check_event_recv).unwrap();
                assert_no_event!(proxy);
                // NOTE: logic added after `assert_no_event!` will not currently be run. this test
                // will need to be updated after ZX-3923 is completed.
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
        let exec = Executor::new().expect("Executor creation failed");
        let scope = ExecutionScope::new(Box::new(exec.ehandle()));

        let server = read_only(async || Ok(b"Read only test".to_vec()));

        run_client(exec, async move || {
            let (proxy, server_end) =
                create_proxy::<FileMarker>().expect("Failed to create connection endpoints");

            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            server.open(scope, flags, 0, Path::empty(), server_end.into_channel().into());

            assert_event!(proxy, FileEvent::OnOpen_ { s, info }, {
                assert_eq!(s, ZX_OK);
                assert_eq!(info, Some(Box::new(NodeInfo::File(FileObject { event: None }))));
            });
        });
    }

    #[test]
    fn write_only_write() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, async move |content| {
                assert_eq!(&*content, b"Write only test");
                Ok(())
            }),
            async move |proxy| {
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
                async || Ok(b"Hello".to_vec()),
                100,
                async move |content| {
                    assert_eq!(*&content, b"Hello, world!");
                    Ok(())
                },
            ),
            async move |proxy| {
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
            read_only({
                let attempts = attempts.clone();
                move || {
                    let attempts = attempts.clone();
                    async move {
                        let read_attempt = attempts.fetch_add(1, Ordering::Relaxed);
                        match read_attempt {
                            0 => Ok(b"State one".to_vec()),
                            _ => panic!("Called init_buffer() a second time."),
                        }
                    }
                }
            }),
            async move |proxy| {
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
            write_only(100, {
                let attempts = attempts.clone();
                move |content| {
                    let attempts = attempts.clone();
                    async move {
                        let write_attempt = attempts.fetch_add(1, Ordering::Relaxed);
                        match write_attempt {
                            0 => {
                                assert_eq!(&*content, b"Write one and two");
                                Ok(())
                            }
                            _ => panic!("Second write() call.  Content: '{:?}'", content),
                        }
                    }
                }
            }),
            async move |proxy| {
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

        let exec = Executor::new().expect("Executor creation failed");
        let scope = ExecutionScope::new(Box::new(exec.ehandle()));

        let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
        let server = read_only({
            let read_attempt = read_attempt.clone();
            move || {
                let read_attempt = read_attempt.clone();
                async move {
                    let attempt = read_attempt.fetch_add(1, Ordering::Relaxed);
                    match attempt {
                        0 => Err(Status::SHOULD_WAIT),
                        1 => Ok(b"Have value".to_vec()),
                        _ => panic!("Third call to read()."),
                    }
                }
            }
        });

        run_client(exec, async move || {
            {
                let (proxy, server_end) =
                    create_proxy::<FileMarker>().expect("Failed to create connection endpoints");

                server.clone().open(
                    scope.clone(),
                    flags,
                    0,
                    Path::empty(),
                    server_end.into_channel().into(),
                );

                assert_event!(proxy, FileEvent::OnOpen_ { s, info }, {
                    assert_eq!(Status::from_raw(s), Status::SHOULD_WAIT);
                    assert_eq!(info, None);
                });
            }

            {
                let (proxy, server_end) =
                    create_proxy::<FileMarker>().expect("Failed to create connection endpoints");

                server.open(scope, flags, 0, Path::empty(), server_end.into_channel().into());

                assert_event!(proxy, FileEvent::OnOpen_ { s, info }, {
                    assert_eq!(s, ZX_OK);
                    assert_eq!(info, Some(Box::new(NodeInfo::File(FileObject { event: None }))));
                });

                assert_read!(proxy, "Have value");
                assert_close!(proxy);
            }
        });

        assert_eq!(read_attempt.load(Ordering::Relaxed), 2);
    }

    #[test]
    fn read_write_no_write_flag() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_write(
                async || Ok(b"Can read".to_vec()),
                100,
                async move |_content| {
                    panic!("File was not opened as writable");
                },
            ),
            async move |proxy| {
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
                async || {
                    panic!("File was not opened as readable");
                },
                100,
                async move |content| {
                    assert_eq!(*&content, b"Can write");
                    Ok(())
                },
            ),
            async move |proxy| {
                assert_read_err!(proxy, Status::ACCESS_DENIED);
                assert_read_at_err!(proxy, 0, Status::ACCESS_DENIED);
                assert_write!(proxy, "Can write");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_write_ignore_posix_flag() {
        let attempts = Arc::new(AtomicUsize::new(0));

        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_POSIX,
            read_write(async || Ok(b"Content".to_vec()), 100, {
                let attempts = attempts.clone();
                move |content| {
                    let attempts = attempts.clone();
                    async move {
                        let write_attempt = attempts.fetch_add(1, Ordering::Relaxed);
                        match write_attempt {
                            0 => {
                                assert_eq!(*&content, b"Can write");
                                Ok(())
                            }
                            _ => panic!("Second write() call.  Content: '{:?}'", content),
                        }
                    }
                }
            }),
            async move |proxy| {
                assert_read!(proxy, "Content");
                assert_seek!(proxy, 0, Start);
                assert_write!(proxy, "Can write");
                assert_seek!(proxy, 0, Start);
                assert_read!(proxy, "Can write");
                assert_close!(proxy);
            },
        );

        assert_eq!(attempts.load(Ordering::Relaxed), 1);
    }

    #[test]
    /// When the `init_buffer` handler returns a value that is larger then the specified capacity
    /// of the file, `update` handler will receive it as is, uncut. This behaviour is specified in
    /// the description of [`PseudoFileImpl::capacity`].
    fn read_returns_more_than_capacity() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                async || Ok(b"Read handler may return more than capacity".to_vec()),
                10,
                async move |content| {
                    assert_eq!(content, b"Write then could write beyond max capacity".to_vec());
                    Ok(())
                },
            ),
            async move |proxy| {
                assert_read!(proxy, "Read");
                assert_seek!(proxy, 0, Start);
                // Need to write something, otherwise `update` handler will not be called.
                // "capacity" is a leftover from what `init_buffer` handler has returned.
                assert_write!(proxy, "Write then could write beyond max");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn write_error() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, async move |content| {
                assert_eq!(*&content, b"Wrong format");
                Err(Status::INVALID_ARGS)
            }),
            async move |proxy| {
                assert_write!(proxy, "Wrong");
                assert_write!(proxy, " format");
                assert_close_err!(proxy, Status::INVALID_ARGS);
            },
        );
    }

    #[test]
    fn open_truncate() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_TRUNCATE,
            read_write(
                async || panic!("OPEN_FLAG_TRUNCATE means read() is not called."),
                100,
                async move |content| {
                    assert_eq!(*&content, b"File content");
                    Ok(())
                },
            ),
            async move |proxy| {
                assert_write!(proxy, "File content");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_at_0() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(async || Ok(b"Whole file content".to_vec())),
            async move |proxy| {
                assert_read_at!(proxy, 0, "Whole file content");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_at_overlapping() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(async || Ok(b"Content of the file".to_vec())),
            //                      0         1
            //                      0123456789012345678
            async move |proxy| {
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
            read_only(async || Ok(b"Content of the file".to_vec())),
            //                      0         1
            //                      0123456789012345678
            async move |proxy| {
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
            write_only(100, async move |content| {
                assert_eq!(*&content, b"File content");
                Ok(())
            }),
            async move |proxy| {
                assert_write_at!(proxy, 0, "File content");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn write_at_overlapping() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, async move |content| {
                assert_eq!(*&content, b"Whole file content");
                //                      0         1
                //                      012345678901234567
                Ok(())
            }),
            async move |proxy| {
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
            write_only(100, async move |content| {
                assert_eq!(*&content, b"Whole file content");
                //                      0         1
                //                      012345678901234567
                Ok(())
            }),
            async move |proxy| {
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
                async || Ok(b"Initial".to_vec()),
                100,
                async move |content| {
                    assert_eq!(*&content, b"Final content");
                    //                      0         1
                    //                      0123456789012
                    Ok(())
                },
            ),
            async move |proxy| {
                assert_read!(proxy, "Init");
                assert_write!(proxy, "l con");
                // buffer: "Initl con"
                assert_seek!(proxy, 0, Start);
                assert_write!(proxy, "Fina");
                // buffer: "Final con"
                assert_seek!(proxy, 0, End, 9);
                assert_write!(proxy, "tent");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn seek_valid_positions() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(async || Ok(b"Long file content".to_vec())),
            //                      0         1
            //                      01234567890123456
            async move |proxy| {
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
                async || Ok(b"Content".to_vec()),
                //            0123456
                100,
                async move |content| {
                    assert_eq!(*&content, b"Content extended further");
                    //                      0         1         2
                    //                      012345678901234567890123
                    Ok(())
                },
            ),
            async move |proxy| {
                assert_seek!(proxy, 7, Start);
                // POSIX wants this to be a zero read. ZX-3633.
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
            },
        );
    }

    #[test]
    fn seek_invalid_before_0() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(async || Ok(b"Seek position is unaffected".to_vec())),
            //                      0         1         2
            //                      012345678901234567890123456
            async move |proxy| {
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
                async || Ok(b"Content".to_vec()),
                //      0123456
                10,
                async move |_content| panic!("No writes should have happened"),
            ),
            async move |proxy| {
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
                async || Ok(b"Content".to_vec()),
                //      0123456
                100,
                async move |content| {
                    assert_eq!(*&content, b"Content\0\0\0end");
                    //                      0            1
                    //                      01234567 8 9 012
                    Ok(())
                },
            ),
            async move |proxy| {
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
                async || Ok(b"Long content".to_vec()),
                //            0         1
                //            012345678901
                8,
                async move |_content| panic!("No writes should have happened"),
            ),
            async move |proxy| {
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
                async || Ok(b"Content".to_vec()),
                //            0123456
                100,
                async move |content| {
                    assert_eq!(*&content, b"Replaced");
                    Ok(())
                },
            ),
            async move |proxy| {
                assert_read!(proxy, "Content");
                assert_truncate!(proxy, 0);
                // truncate should not change the seek position.
                assert_seek!(proxy, 0, Current, 7);
                assert_seek!(proxy, 0, Start);
                assert_write!(proxy, "Replaced");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn write_then_truncate() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, async move |content| {
                assert_eq!(*&content, b"Replaced");
                Ok(())
            }),
            async move |proxy| {
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
            write_only(10, async move |_content| panic!("No writes should have happened")),
            async move |proxy| {
                assert_truncate_err!(proxy, 20, Status::OUT_OF_RANGE);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn truncate_read_only_file() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(async || Ok(b"Read-only content".to_vec())),
            async move |proxy| {
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
                async || Ok(b"Content is very long".to_vec()),
                //            0         1
                //            01234567890123456789
                10,
                async move |content| {
                    assert_eq!(*&content, b"Content is very");
                    //                      0         1
                    //                      012345678901234
                    Ok(())
                },
            ),
            async move |proxy| {
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
                async || Ok(b"Initial content".to_vec()),
                100,
                async move |content| {
                    assert_eq!(*&content, b"As updated");
                    Ok(())
                },
            ),
            async move |first_proxy| {
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
            },
        );
    }

    #[test]
    fn clone_inherit_access() {
        use fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS;

        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                async || Ok(b"Initial content".to_vec()),
                100,
                async move |content| {
                    assert_eq!(*&content, b"As updated");
                    Ok(())
                },
            ),
            async move |first_proxy| {
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
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(async || Ok(b"Content".to_vec())),
            async move |proxy| {
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
            },
        );
    }

    #[test]
    fn get_attr_write_only() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(10, async move |_content| panic!("No changes")),
            async move |proxy| {
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
            read_write(
                async || Ok(b"Content".to_vec()),
                10,
                async move |_content| panic!("No changes"),
            ),
            async move |proxy| {
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
    fn clone_cannot_increase_access() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_write(
                async || Ok(b"Initial content".to_vec()),
                100,
                async move |_content| {
                    panic!("Clone should not be able to write.");
                },
            ),
            async move |first_proxy| {
                assert_read!(first_proxy, "Initial content");
                assert_write_err!(first_proxy, "Write attempt", Status::ACCESS_DENIED);

                let second_proxy = clone_as_file_assert_err!(
                    &first_proxy,
                    OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE,
                    Status::ACCESS_DENIED
                );

                assert_read_fidl_err!(second_proxy, fidl::Error::ClientWrite(Status::PEER_CLOSED));
                assert_write_fidl_err!(
                    second_proxy,
                    "Write attempt",
                    fidl::Error::ClientWrite(Status::PEER_CLOSED)
                );

                assert_close!(first_proxy);
            },
        );
    }

    #[test]
    fn node_reference_ignores_read_access() {
        run_server_client(
            OPEN_FLAG_NODE_REFERENCE | OPEN_RIGHT_READABLE,
            read_only(async || panic!("Not supposed to read!")),
            async move |proxy| {
                assert_read_err!(proxy, Status::ACCESS_DENIED);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn node_reference_ignores_write_access() {
        run_server_client(
            OPEN_FLAG_NODE_REFERENCE | OPEN_RIGHT_WRITABLE,
            write_only(100, async move |_content| panic!("Not supposed to write!")),
            async move |proxy| {
                assert_write_err!(proxy, "Can write", Status::ACCESS_DENIED);
                assert_close!(proxy);
            },
        );
    }

    /// This test checks a somewhat non-trivial case. Two clients are connected to the same file,
    /// and we want to make sure that they get individual buffers. The file content will be
    /// different every time a new buffer is created, as `init_buffer` returns a string with an
    /// invocation count in it.
    ///
    /// [`run_server_client_with_executor`] is used to control relative execution of the clients
    /// and the server. Clients wait before they open the file, read the file content and then wait
    /// before reading the file content once again.
    ///
    /// `run_until_stalled` and `oneshot::channel` are used to make sure that the test execution
    /// does not have race conditions. We check that the futures are still running and check the
    /// `init_buffer` invocation counter. See `coordinator` argument of the
    /// `run_server_client_with_executor` invocation.
    #[test]
    fn mock_directory_with_one_file_and_two_connections() {
        let exec = Executor::new().expect("Executor creation failed");
        let scope = ExecutionScope::new(Box::new(exec.ehandle()));

        let read_count = Arc::new(AtomicUsize::new(0));
        let server = read_only({
            let read_count = read_count.clone();
            move || {
                let read_count = read_count.clone();
                async move {
                    let count = read_count.fetch_add(1, Ordering::Relaxed);
                    Ok(format!("Content {}", count).into_bytes())
                }
            }
        });

        let create_client = move |expected_content: &'static str| {
            let (proxy, server_end) =
                create_proxy::<FileMarker>().expect("Failed to create connection endpoints");

            let (start_sender, start_receiver) = oneshot::channel::<()>();
            let (read_and_close_sender, read_and_close_receiver) = oneshot::channel::<()>();

            let server = server.clone();
            let scope = scope.clone();

            (
                async move || {
                    await!(start_receiver).unwrap();

                    server.open(
                        scope,
                        OPEN_RIGHT_READABLE,
                        0,
                        Path::empty(),
                        server_end.into_channel().into(),
                    );

                    assert_read!(proxy, expected_content);

                    await!(read_and_close_receiver).unwrap();

                    assert_seek!(proxy, 0, Start);
                    assert_read!(proxy, expected_content);
                    assert_close!(proxy);
                },
                move || {
                    start_sender.send(()).unwrap();
                },
                move || {
                    read_and_close_sender.send(()).unwrap();
                },
            )
        };

        let (get_client1, client1_start, client1_read_and_close) = create_client("Content 0");
        let (get_client2, client2_start, client2_read_and_close) = create_client("Content 1");

        run_client_with_executor(
            exec,
            async move || {
                let client1 = get_client1();
                let client2 = get_client2();

                let _ = await!(join(client1, client2));
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
    fn slow_init_buffer() {
        // This test creates an init_buffer future that doesn't return `Ready` with the result of
        // the init_buffer operation until it receives a signal on a oneshot channel. We confirm
        // the behavior we expect from the file - notably that we are able to send multiple
        // requests to the file before the connection is actually created and populated, and have
        // them be executed once the buffer is filled with what we expect.
        let exec = Executor::new().expect("Executor creation failed");

        let read_counter = Arc::new(AtomicUsize::new(0));
        let client_counter = Arc::new(AtomicUsize::new(0));
        let (finish_future_sender, finish_future_receiver) = oneshot::channel::<()>();
        let finish_future_receiver = finish_future_receiver.shared();

        run_server_client_with_executor(
            OPEN_RIGHT_READABLE,
            exec,
            read_only({
                let read_counter = read_counter.clone();
                move || {
                    let read_counter = read_counter.clone();
                    let finish_future_receiver = finish_future_receiver.clone();
                    async move {
                        read_counter.fetch_add(1, Ordering::Relaxed);
                        await!(finish_future_receiver)
                            .expect("finish_future_sender was not called before been dropped.");
                        read_counter.fetch_add(1, Ordering::Relaxed);
                        Ok(b"content".to_vec())
                    }
                }
            }),
            {
                let client_counter = client_counter.clone();
                async move |proxy| {
                    client_counter.fetch_add(1, Ordering::Relaxed);

                    assert_read!(proxy, "content");

                    assert_seek!(proxy, 4, Start);
                    assert_read!(proxy, "ent");
                    assert_close!(proxy);

                    client_counter.fetch_add(1, Ordering::Relaxed);
                }
            },
            |run_until_stalled_assert| {
                let check_read_client_counts = |expected_read, expected_client| {
                    assert_eq!(read_counter.load(Ordering::Relaxed), expected_read);
                    assert_eq!(client_counter.load(Ordering::Relaxed), expected_client);
                };
                run_until_stalled_assert(false);

                // init_buffer is waiting yet, as well as the client.
                check_read_client_counts(1, 1);

                finish_future_sender.send(()).unwrap();
                run_until_stalled_assert(true);

                // Both have reached the end.
                check_read_client_counts(2, 2);
            },
        );
    }

    #[test]
    fn slow_update() {
        // this test is pretty similar to the above, except that it lags the update call instead.
        let exec = Executor::new().expect("Executor creation failed");

        let write_counter = Arc::new(AtomicUsize::new(0));
        let client_counter = Arc::new(AtomicUsize::new(0));
        let (finish_future_sender, finish_future_receiver) = oneshot::channel::<()>();
        let finish_future_receiver = finish_future_receiver.shared();

        run_server_client_with_executor(
            OPEN_RIGHT_WRITABLE,
            exec,
            write_only(100, {
                let write_counter = write_counter.clone();
                let finish_future_receiver = finish_future_receiver.shared();
                move |content| {
                    let write_counter = write_counter.clone();
                    let finish_future_receiver = finish_future_receiver.clone();
                    async move {
                        assert_eq!(*&content, b"content");
                        write_counter.fetch_add(1, Ordering::Relaxed);
                        await!(finish_future_receiver)
                            .expect("finish_future_sender was not called before been dropped.");
                        write_counter.fetch_add(1, Ordering::Relaxed);
                        Ok(())
                    }
                }
            }),
            {
                let client_counter = client_counter.clone();
                async move |proxy| {
                    client_counter.fetch_add(1, Ordering::Relaxed);

                    assert_write!(proxy, "content");
                    assert_close!(proxy);

                    client_counter.fetch_add(1, Ordering::Relaxed);
                }
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
