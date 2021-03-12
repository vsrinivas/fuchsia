// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests for the asynchronous files.

use super::write_only;

// Macros are exported into the root of the crate.
use crate::{
    assert_close, assert_close_err, assert_get_attr, assert_read, assert_read_at, assert_seek,
    assert_seek_err, assert_truncate, assert_truncate_err, assert_write, assert_write_at,
    assert_write_err,
};

use crate::{
    directory::entry::DirectoryEntry,
    file::test_utils::{run_server_client, test_server_client},
    file::vmo,
};

use {
    fidl_fuchsia_io::{
        NodeAttributes, INO_UNKNOWN, MODE_TYPE_FILE, OPEN_FLAG_NODE_REFERENCE, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon::Status,
    futures::{channel::oneshot, FutureExt},
    libc::{S_IRUSR, S_IWUSR},
    parking_lot::Mutex,
    std::sync::{
        atomic::{AtomicUsize, Ordering},
        Arc,
    },
};

fn read_only_static<Bytes: 'static>(bytes: Bytes) -> Arc<dyn DirectoryEntry>
where
    Bytes: AsRef<[u8]> + Send + Sync,
{
    return vmo::asynchronous::read_only_static(bytes);
}

#[test]
fn write_only_write() {
    run_server_client(
        OPEN_RIGHT_WRITABLE,
        write_only(100, |content| async move {
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
        |proxy| async move {
            assert_write!(proxy, "Write one");
            assert_write!(proxy, " and two");
            assert_close!(proxy);
        },
    );

    assert_eq!(attempts.load(Ordering::Relaxed), 1);
}

#[test]
fn write_error() {
    run_server_client(
        OPEN_RIGHT_WRITABLE,
        write_only(100, |content| async move {
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
    let write_call_sender = Mutex::new(Some(write_call_sender));
    run_server_client(
        OPEN_RIGHT_WRITABLE,
        write_only(100, move |content| {
            let mut lock = write_call_sender.lock();
            let write_call_sender = lock.take().unwrap();
            async move {
                assert_eq!(*&content, b"Updated content");
                write_call_sender.send(()).unwrap();
                Ok(())
            }
        }),
        move |proxy| async move {
            assert_write!(proxy, "Updated content");
            drop(proxy);
            write_call_receiver.await.unwrap();
        },
    );
}

#[test]
fn read_at_0() {
    run_server_client(
        OPEN_RIGHT_READABLE,
        read_only_static(b"Whole file content"),
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
        read_only_static(b"Content of the file"),
        //                 0         1
        //                 0123456789012345678
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
        read_only_static(b"Content of the file"),
        //                 0         1
        //                 0123456789012345678
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
        write_only(100, |content| async move {
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
            async move {
                assert_eq!(*&content, b"Whole file content");
                //                      0         1
                //                      012345678901234567
                Ok(())
            }
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
            async move {
                assert_eq!(*&content, b"Whole file content");
                //                      0         1
                //                      012345678901234567
                Ok(())
            }
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
fn seek_valid_positions() {
    run_server_client(
        OPEN_RIGHT_READABLE,
        read_only_static(b"Long file content"),
        //                 0         1
        //                 01234567890123456
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
fn seek_invalid_before_0() {
    run_server_client(
        OPEN_RIGHT_READABLE,
        read_only_static(b"Seek position is unaffected"),
        //                 0         1         2
        //                 012345678901234567890123456
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
fn write_then_truncate() {
    run_server_client(
        OPEN_RIGHT_WRITABLE,
        write_only(100, |content| async move {
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
        write_only(10, |_content| async move { panic!("No writes should have happened") }),
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
        read_only_static(b"Read-only content"),
        |proxy| async move {
            assert_truncate_err!(proxy, 10, Status::BAD_HANDLE);
            assert_close!(proxy);
        },
    );
}

#[test]
fn get_attr_read_only() {
    let content = b"Content";
    let content_len = content.len() as u64;
    run_server_client(OPEN_RIGHT_READABLE, read_only_static(content), |proxy| async move {
        assert_get_attr!(
            proxy,
            NodeAttributes {
                mode: MODE_TYPE_FILE | S_IRUSR,
                id: INO_UNKNOWN,
                content_size: content_len,
                storage_size: content_len,
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
        write_only(10, |_content| async move { panic!("No changes") }),
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
fn node_reference_ignores_write_access() {
    run_server_client(
        OPEN_FLAG_NODE_REFERENCE | OPEN_RIGHT_WRITABLE,
        write_only(100, |_content| async move { panic!("Not supposed to write!") }),
        |proxy| async move {
            assert_write_err!(proxy, "Can write", Status::BAD_HANDLE);
            assert_close!(proxy);
        },
    );
}

#[test]
fn node_reference_can_not_seek() {
    run_server_client(OPEN_FLAG_NODE_REFERENCE, read_only_static(b"Content"), |proxy| async move {
        assert_seek_err!(proxy, 0, Current, Status::BAD_HANDLE, 0);
        assert_close!(proxy);
    });
}

/// This test is very similar to the `slow_init_buffer` above, except that it lags the update call
/// instead.
#[test]
fn slow_update() {
    let write_counter = Arc::new(AtomicUsize::new(0));
    let client_counter = Arc::new(AtomicUsize::new(0));
    let (finish_future_sender, finish_future_receiver) = oneshot::channel::<()>();
    let finish_future_receiver = finish_future_receiver.shared();

    test_server_client(
        OPEN_RIGHT_WRITABLE,
        write_only(100, {
            let write_counter = write_counter.clone();
            let finish_future_receiver = finish_future_receiver.shared();
            move |content| {
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
            }
        }),
        {
            let client_counter = client_counter.clone();
            |proxy| async move {
                client_counter.fetch_add(1, Ordering::Relaxed);

                assert_write!(proxy, "content");
                assert_close!(proxy);

                client_counter.fetch_add(1, Ordering::Relaxed);
            }
        },
    )
    .coordinator(|mut controller| {
        let check_write_client_counts = |expected_write, expected_client| {
            assert_eq!(write_counter.load(Ordering::Relaxed), expected_write);
            assert_eq!(client_counter.load(Ordering::Relaxed), expected_client);
        };

        controller.run_until_stalled();

        // The server and the client are waiting.
        check_write_client_counts(1, 1);

        finish_future_sender.send(()).unwrap();
        controller.run_until_complete();

        // The server and the client are done.
        check_write_client_counts(2, 2);
    })
    .run();
}
