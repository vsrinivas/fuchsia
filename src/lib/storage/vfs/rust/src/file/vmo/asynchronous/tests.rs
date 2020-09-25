// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests for the asynchronous files.

use super::{read_only, read_write, write_only, NewVmo};

// Macros are exported into the root of the crate.
use crate::{
    assert_close, assert_event, assert_get_attr, assert_get_buffer, assert_get_buffer_err,
    assert_no_event, assert_read, assert_read_at, assert_read_at_err, assert_read_err,
    assert_read_fidl_err_closed, assert_seek, assert_seek_err, assert_truncate,
    assert_truncate_err, assert_vmo_content, assert_write, assert_write_at, assert_write_at_err,
    assert_write_err, assert_write_fidl_err_closed, clone_as_file_assert_err,
    clone_get_file_proxy_assert_ok, clone_get_proxy_assert, report_invalid_vmo_content,
};

use crate::{
    directory::entry::DirectoryEntry,
    execution_scope::ExecutionScope,
    file::test_utils::{run_client, run_server_client, test_client, test_server_client},
    path::Path,
};

use super::test_utils::{
    consume_vmo_with_counter, simple_consume_vmo, simple_init_vmo, simple_init_vmo_resizable,
    simple_init_vmo_resizable_with_capacity, simple_init_vmo_with_capacity, simple_read_only,
    simple_read_write, simple_read_write_resizeable, simple_write_only,
    simple_write_only_with_capacity,
};

use {
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io::{
        FileEvent, FileMarker, FileObject, NodeAttributes, NodeInfo, INO_UNKNOWN, MODE_TYPE_FILE,
        OPEN_FLAG_DESCRIBE, OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_POSIX, OPEN_FLAG_TRUNCATE,
        OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE, VMO_FLAG_EXACT, VMO_FLAG_PRIVATE, VMO_FLAG_READ,
        VMO_FLAG_WRITE,
    },
    fuchsia_async::Executor,
    fuchsia_zircon::{sys::ZX_OK, Status, Vmo},
    futures::{channel::oneshot, future::join, FutureExt},
    libc::{S_IRUSR, S_IWUSR},
    parking_lot::Mutex,
    std::sync::{
        atomic::{AtomicUsize, Ordering},
        Arc,
    },
};

#[test]
fn read_only_read() {
    run_server_client(
        OPEN_RIGHT_READABLE,
        simple_read_only(b"Read only test"),
        |proxy| async move {
            assert_read!(proxy, "Read only test");
            assert_close!(proxy);
        },
    );
}

#[test]
fn read_only_ignore_posix_flag() {
    run_server_client(
        OPEN_RIGHT_READABLE | OPEN_FLAG_POSIX,
        read_only(simple_init_vmo(b"Content")),
        |proxy| async move {
            assert_read!(proxy, "Content");
            assert_write_err!(proxy, "Can write", Status::ACCESS_DENIED);
            assert_close!(proxy);
        },
    );
}

#[test]
fn read_only_read_no_status() {
    let (check_event_send, check_event_recv) = oneshot::channel::<()>();

    test_server_client(OPEN_RIGHT_READABLE, simple_read_only(b"Read only test"), |proxy| {
        async move {
            // Make sure `open()` call is complete, before we start checking.
            check_event_recv.await.unwrap();
            assert_no_event!(proxy);
            // NOTE: logic added after `assert_no_event!` will not currently be run. this test will
            // need to be updated after fxbug.dev/33709 is completed.
        }
    })
    .coordinator(|mut controller| {
        controller.run_until_stalled();
        check_event_send.send(()).unwrap();
        controller.run_until_stalled_and_forget();
    })
    .run();
}

#[test]
fn read_only_read_with_describe() {
    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::new();

    let server = simple_read_only(b"Read only test");

    run_client(exec, || async move {
        let (proxy, server_end) =
            create_proxy::<FileMarker>().expect("Failed to create connection endpoints");

        let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
        server.open(scope, flags, 0, Path::empty(), server_end.into_channel().into());

        assert_event!(proxy, FileEvent::OnOpen_ { s, info }, {
            assert_eq!(s, ZX_OK);
            assert_eq!(
                info,
                Some(Box::new(NodeInfo::File(FileObject { event: None, stream: None })))
            );
        });
    });
}

#[test]
fn write_only_write() {
    run_server_client(
        OPEN_RIGHT_WRITABLE,
        simple_write_only(b"Write only test"),
        |proxy| async move {
            assert_write!(proxy, "Write only test");
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
                        0 => {
                            let content = b"State one";
                            let capacity = content.len() as u64;
                            let vmo = Vmo::create(capacity)?;
                            vmo.write(content, 0)?;
                            Ok(NewVmo { vmo, size: capacity, capacity })
                        }
                        _ => panic!("Called init_vmo() a second time."),
                    }
                }
            }
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
        write_only(simple_init_vmo_with_capacity(b"", 100), {
            let attempts = attempts.clone();
            move |vmo| {
                let attempts = attempts.clone();
                async move {
                    let write_attempt = attempts.fetch_add(1, Ordering::Relaxed);
                    match write_attempt {
                        0 => {
                            assert_vmo_content!(&vmo, b"Write one and two\0");
                        }
                        _ => {
                            report_invalid_vmo_content!(&vmo, "Second consume_vmo() call");
                        }
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
fn read_error() {
    let read_attempt = Arc::new(AtomicUsize::new(0));

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::new();

    let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
    let server = read_only({
        let read_attempt = read_attempt.clone();
        move || {
            let read_attempt = read_attempt.clone();
            async move {
                let attempt = read_attempt.fetch_add(1, Ordering::Relaxed);
                match attempt {
                    0 => Err(Status::SHOULD_WAIT),
                    1 => {
                        let content = b"Have value";
                        let capacity = content.len() as u64;
                        let vmo = Vmo::create(capacity)?;
                        vmo.write(content, 0)?;
                        Ok(NewVmo { vmo, size: capacity, capacity })
                    }
                    _ => panic!("Third call to read()."),
                }
            }
        }
    });

    run_client(exec, || async move {
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
                assert_eq!(
                    info,
                    Some(Box::new(NodeInfo::File(FileObject { event: None, stream: None })))
                );
            });

            assert_read!(proxy, "Have value");
            assert_close!(proxy);
        }
    });

    assert_eq!(read_attempt.load(Ordering::Relaxed), 2);
}

#[test]
fn read_write_no_write_flag() {
    let consume_attempt = Arc::new(AtomicUsize::new(0));

    run_server_client(
        OPEN_RIGHT_READABLE,
        read_write(simple_init_vmo_resizable(b"Can read"), {
            let consume_attempt = consume_attempt.clone();
            move |vmo| {
                let consume_attempt = consume_attempt.clone();
                async move {
                    let consume_attempt = consume_attempt.fetch_add(1, Ordering::Relaxed);
                    match consume_attempt {
                        0 => {
                            assert_vmo_content!(&vmo, b"Can read\0");
                        }
                        _ => {
                            report_invalid_vmo_content!(&vmo, "Second consume_vmo() call");
                        }
                    }
                }
            }
        }),
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
        read_write(simple_init_vmo_resizable(b""), simple_consume_vmo(b"Can write\0\0")),
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
    let attempts = Arc::new(AtomicUsize::new(0));

    run_server_client(
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_POSIX,
        read_write(
            simple_init_vmo_resizable(b"Content"),
            consume_vmo_with_counter(b"Can write", attempts.clone(), 1, "`consume_vmo`"),
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

    assert_eq!(attempts.load(Ordering::Relaxed), 1);
}

#[test]
/// When the `init_vmo` createa a VMO that is larger then the specified capacity of the file, the
/// user will be able to access all of the allocated bytes.  More details in
/// [`file::vmo::asynchronous::NewVmo::capacity`].
fn read_returns_more_than_capacity() {
    run_server_client(
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        read_write(
            simple_init_vmo_resizable_with_capacity(
                b"`init_vmo` returns a VMO larger than capacity",
                10,
            ),
            simple_consume_vmo(b"     Write then can write beyond max capacity"),
        ),
        |proxy| async move {
            assert_read!(proxy, "`init_vmo` returns");
            assert_read!(proxy, " a VMO larger");
            assert_seek!(proxy, 0, Start);
            assert_write!(proxy, "     Write then can write beyond max");
            assert_close!(proxy);
        },
    );
}

#[test]
fn write_and_drop_connection() {
    let (consume_vmo_call_tx, consume_vmo_call_rx) = oneshot::channel::<()>();
    let consume_vmo_call_tx = Mutex::new(Some(consume_vmo_call_tx));
    run_server_client(
        OPEN_RIGHT_WRITABLE,
        write_only(simple_init_vmo_resizable(b""), move |vmo| {
            let mut lock = consume_vmo_call_tx.lock();
            let consume_vmo_call_tx = lock.take().unwrap();
            Box::pin(async move {
                assert_vmo_content!(&vmo, b"Updated content");
                consume_vmo_call_tx.send(()).unwrap();
            })
        }),
        move |proxy| async move {
            assert_write!(proxy, "Updated content");
            drop(proxy);
            consume_vmo_call_rx.await.unwrap();
        },
    );
}

#[test]
fn open_truncate() {
    run_server_client(
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_TRUNCATE,
        read_write(
            simple_init_vmo_resizable(b"Will be erased"),
            simple_consume_vmo(b"File content"),
        ),
        |proxy| {
            async move {
                // Seek to the end to check the current size.
                assert_seek!(proxy, 0, End, 0);
                assert_write!(proxy, "File content");
                assert_close!(proxy);
            }
        },
    );
}

#[test]
fn read_at_0() {
    run_server_client(
        OPEN_RIGHT_READABLE,
        simple_read_only(b"Whole file content"),
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
        simple_read_only(b"Content of the file"),
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
        simple_read_only(b"Content of the file"),
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
        simple_write_only(b"File content"),
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
        simple_write_only(b"Whole file content"),
        //                  0         1
        //                  012345678901234567
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
        simple_write_only(b"Whole file content"),
        //                  0         1
        //                  012345678901234567
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
        simple_read_write(
            b"Initial",
            b"Final content",
            // 0        1
            // 123456789012
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
        simple_read_only(b"Long file content"),
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
fn seek_valid_after_size_before_capacity() {
    run_server_client(
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        simple_read_write(
            b"Content",
            // 123456
            b"Content extended further",
            // 0        1         2
            // 12345678901234567890123
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
        read_only(simple_init_vmo_with_capacity(
            b"Seek position is unaffected",
            // 0        1         2
            // 12345678901234567890123456
            50,
        )),
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
        OPEN_RIGHT_READABLE,
        read_only(simple_init_vmo_with_capacity(b"Content", 10)),
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
            simple_init_vmo_resizable(b"Content"),
            //                          0123456
            simple_consume_vmo(b"Content\0\0\0end"),
            //                   0            1
            //                   01234567 8 9 012
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
/// Make sure that even if the file content is larger than the capacity, seek does not allow to go
/// beyond the maximum of the capacity and length.
fn seek_beyond_capacity_in_large_file() {
    run_server_client(
        OPEN_RIGHT_READABLE,
        read_only(simple_init_vmo_with_capacity(
            b"Long content",
            // 0        1
            // 12345678901
            8,
        )),
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
        read_write(simple_init_vmo_resizable(b"Content"), simple_consume_vmo(b"Replaced")),
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
    run_server_client(OPEN_RIGHT_WRITABLE, simple_write_only(b"Replaced"), |proxy| async move {
        assert_write!(proxy, "Replaced content");
        assert_truncate!(proxy, 8);
        assert_close!(proxy);
    });
}

#[test]
fn truncate_beyond_capacity() {
    run_server_client(
        OPEN_RIGHT_WRITABLE,
        simple_write_only_with_capacity(10, b""),
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
        simple_read_only(b"Read-only content"),
        |proxy| async move {
            assert_truncate_err!(proxy, 10, Status::ACCESS_DENIED);
            assert_close!(proxy);
        },
    );
}

#[test]
/// Make sure that when the `init_vmo` has returned a buffer that is larger than the capacity, we
/// can cut it down to a something that is still larger then the capacity.  But we can not undo
/// that cut.
fn truncate_large_file_beyond_capacity() {
    run_server_client(
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        read_write(
            simple_init_vmo_resizable_with_capacity(
                b"Content is very long",
                // 0        1
                // 1234567890123456789
                10,
            ),
            simple_consume_vmo(b"Content is very"),
            //                   0         1
            //                   012345678901234
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
        simple_read_write_resizeable(b"Initial content", b"As updated"),
        |first_proxy| async move {
            assert_read!(first_proxy, "Initial content");
            assert_truncate!(first_proxy, 0);
            assert_seek!(first_proxy, 0, Start);
            assert_write!(first_proxy, "As updated");

            let second_proxy = clone_get_file_proxy_assert_ok!(
                &first_proxy,
                OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE
            );

            assert_read!(second_proxy, "As updated");
            assert_truncate_err!(second_proxy, 0, Status::ACCESS_DENIED);
            assert_write_err!(second_proxy, "Overwritten", Status::ACCESS_DENIED);

            assert_close!(first_proxy);
        },
    );
}

#[test]
fn clone_inherit_access() {
    use fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS;

    run_server_client(
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        simple_read_write_resizeable(b"Initial content", b"Overwritten"),
        |first_proxy| async move {
            assert_read!(first_proxy, "Initial content");
            assert_truncate!(first_proxy, 0);
            assert_seek!(first_proxy, 0, Start);
            assert_write!(first_proxy, "As updated");

            let second_proxy = clone_get_file_proxy_assert_ok!(
                &first_proxy,
                CLONE_FLAG_SAME_RIGHTS | OPEN_FLAG_DESCRIBE
            );

            assert_read!(second_proxy, "As updated");
            assert_truncate!(second_proxy, 0);
            assert_seek!(second_proxy, 0, Start);
            assert_write!(second_proxy, "Overwritten");

            assert_close!(first_proxy);
            assert_close!(second_proxy);
        },
    );
}

#[test]
fn get_attr_read_only() {
    run_server_client(OPEN_RIGHT_READABLE, simple_read_only(b"Content"), |proxy| async move {
        assert_get_attr!(
            proxy,
            NodeAttributes {
                mode: MODE_TYPE_FILE | S_IRUSR,
                id: INO_UNKNOWN,
                content_size: 7,
                storage_size: 100,
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
    run_server_client(OPEN_RIGHT_WRITABLE, simple_write_only(b""), |proxy| async move {
        assert_get_attr!(
            proxy,
            NodeAttributes {
                mode: MODE_TYPE_FILE | S_IWUSR,
                id: INO_UNKNOWN,
                content_size: 0,
                storage_size: 100,
                link_count: 1,
                creation_time: 0,
                modification_time: 0,
            }
        );
        assert_close!(proxy);
    });
}

#[test]
fn get_attr_read_write() {
    run_server_client(
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        simple_read_write(b"Content", b"Content"),
        |proxy| async move {
            assert_get_attr!(
                proxy,
                NodeAttributes {
                    mode: MODE_TYPE_FILE | S_IWUSR | S_IRUSR,
                    id: INO_UNKNOWN,
                    content_size: 7,
                    storage_size: 100,
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
        simple_read_only(b"Initial content"),
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
        read_only(simple_init_vmo(b"")),
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
        write_only(simple_init_vmo(b""), simple_consume_vmo(b"")),
        |proxy| async move {
            assert_write_err!(proxy, "Can write", Status::ACCESS_DENIED);
            assert_close!(proxy);
        },
    );
}

#[test]
fn clone_can_not_remove_node_reference() {
    run_server_client(
        OPEN_FLAG_NODE_REFERENCE | OPEN_RIGHT_READABLE,
        read_write(simple_init_vmo(b""), simple_consume_vmo(b"")),
        |first_proxy| {
            async move {
                // first_proxy would not have OPEN_RIGHT_READABLE, as it will be dropped by the
                // OPEN_FLAG_NODE_REFERENCE.  Even though we do not ask for
                // OPEN_FLAG_NODE_REFERENCE here it is actually enforced.  Our
                // OPEN_RIGHT_READABLE is beyond the allowed rights, but it is unrelated to the
                // OPEN_FLAG_NODE_REFERENCE, really.
                let second_proxy = clone_as_file_assert_err!(
                    &first_proxy,
                    OPEN_FLAG_DESCRIBE | OPEN_RIGHT_READABLE,
                    Status::ACCESS_DENIED
                );

                assert_read_fidl_err_closed!(second_proxy);
                assert_write_fidl_err_closed!(second_proxy, "Write attempt");

                // We now try without OPEN_RIGHT_READABLE, as we might still be able to Seek.
                let third_proxy = clone_get_file_proxy_assert_ok!(&first_proxy, OPEN_FLAG_DESCRIBE);

                assert_seek_err!(third_proxy, 0, Current, Status::ACCESS_DENIED, 0);

                assert_close!(third_proxy);
                assert_close!(first_proxy);
            }
        },
    );
}

#[test]
fn node_reference_can_not_seek() {
    run_server_client(
        OPEN_FLAG_NODE_REFERENCE,
        read_only(simple_init_vmo(b"Content")),
        |proxy| async move {
            assert_seek_err!(proxy, 0, Current, Status::ACCESS_DENIED, 0);
            assert_close!(proxy);
        },
    );
}

/// This test checks a somewhat non-trivial case. Two clients are connected to the same file, and
/// we want to make sure that they see the same content. The file content will be initially set by
/// the `init_vmo` callback.
///
/// A `coordinator` is used to control relative execution of the clients and the server. Clients
/// wait before they open the file, read the file content and then wait before reading the file
/// content once again.
#[test]
fn mock_directory_with_one_file_and_two_connections() {
    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::new();

    let server = simple_read_write_resizeable(b"Initial", b"Second update");

    let create_client = move |initial_content: &'static str,
                              after_wait_content: &'static str,
                              update_with: &'static str| {
        let (proxy, server_end) =
            create_proxy::<FileMarker>().expect("Failed to create connection endpoints");

        let (start_tx, start_rx) = oneshot::channel::<()>();
        let (write_and_close_tx, write_and_close_rx) = oneshot::channel::<()>();

        let server = server.clone();
        let scope = scope.clone();

        (
            move || async move {
                start_rx.await.unwrap();

                server.open(
                    scope,
                    OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                    0,
                    Path::empty(),
                    server_end.into_channel().into(),
                );

                assert_read!(proxy, initial_content);

                write_and_close_rx.await.unwrap();

                assert_seek!(proxy, 0, Start);
                assert_read!(proxy, after_wait_content);

                assert_truncate!(proxy, 0);
                assert_seek!(proxy, 0, Start);
                assert_write!(proxy, update_with);

                assert_close!(proxy);
            },
            move || {
                start_tx.send(()).unwrap();
            },
            move || {
                write_and_close_tx.send(()).unwrap();
            },
        )
    };

    let (get_client1, client1_start, client1_write_and_close) =
        create_client("Initial", "Initial", "First update");
    let (get_client2, client2_start, client2_write_and_close) =
        create_client("Initial", "First update", "Second updated");

    test_client(|| async move {
        let client1 = get_client1();
        let client2 = get_client2();

        let _ = join(client1, client2).await;
    })
    .exec(exec)
    .coordinator(|mut controller| {
        client1_start();

        client2_start();

        controller.run_until_stalled();

        client1_write_and_close();

        controller.run_until_stalled();

        client2_write_and_close();
    })
    .run();
}

/// This test uses an `init_vmo` callback that does not immediately return a result and instead
/// waits for a signal before continuing.  We confirm the behavior we expect from the file -
/// notably that we are able to send multiple requests to the file before the connection is
/// actually created and populated, and have them be executed once the buffer is filled with what
/// we expect.
#[test]
fn slow_init_vmo() {
    let init_vmo_counter = Arc::new(AtomicUsize::new(0));
    let client_counter = Arc::new(AtomicUsize::new(0));
    let (finish_init_vmo_tx, finish_init_vmo_rx) = oneshot::channel::<()>();
    let finish_init_vmo_rx = finish_init_vmo_rx.shared();

    test_server_client(
        OPEN_RIGHT_READABLE,
        read_only({
            let init_vmo_counter = init_vmo_counter.clone();
            move || {
                let init_vmo_counter = init_vmo_counter.clone();
                let finish_init_vmo_rx = finish_init_vmo_rx.clone();
                Box::pin(async move {
                    init_vmo_counter.fetch_add(1, Ordering::Relaxed);
                    finish_init_vmo_rx
                        .await
                        .expect("finish_init_vmo_tx was not called before been dropped.");
                    init_vmo_counter.fetch_add(1, Ordering::Relaxed);

                    let content = b"Content";

                    let size = content.len() as u64;
                    let capacity = 100;
                    let vmo_size = std::cmp::max(size, capacity);
                    let vmo = Vmo::create(vmo_size)?;
                    vmo.write(content, 0)?;
                    Ok(NewVmo { vmo, size, capacity })
                })
            }
        }),
        {
            let client_counter = client_counter.clone();
            |proxy| async move {
                client_counter.fetch_add(1, Ordering::Relaxed);

                assert_read!(proxy, "Content");

                assert_seek!(proxy, 4, Start);
                assert_read!(proxy, "ent");
                assert_close!(proxy);

                client_counter.fetch_add(1, Ordering::Relaxed);
            }
        },
    )
    .coordinator(|mut controller| {
        let check_init_vmo_client_counts = |expected_init_vmo, expected_client| {
            assert_eq!(init_vmo_counter.load(Ordering::Relaxed), expected_init_vmo);
            assert_eq!(client_counter.load(Ordering::Relaxed), expected_client);
        };
        controller.run_until_stalled();

        // init_vmo is waiting, as well as the client.
        check_init_vmo_client_counts(1, 1);

        finish_init_vmo_tx.send(()).unwrap();
        controller.run_until_complete();

        // Both have reached the end.
        check_init_vmo_client_counts(2, 2);
    })
    .run();
}

/// This test is very similar to the `slow_init_vmo` above, except that it lags the `consume_vmo`
/// call instead.  We want to observer the client connection been closed before `consume_vmo`
/// completes.
#[test]
fn slow_consume_vmo() {
    let consume_vmo_counter = Arc::new(AtomicUsize::new(0));
    let client_counter = Arc::new(AtomicUsize::new(0));
    let (finish_consume_vmo_tx, finish_consume_vmo_rx) = oneshot::channel::<()>();
    let finish_consume_vmo_rx = finish_consume_vmo_rx.shared();

    let (finish_client_tx, finish_client_rx) = oneshot::channel::<()>();

    test_server_client(
        OPEN_RIGHT_WRITABLE,
        write_only(simple_init_vmo(b""), {
            let consume_vmo_counter = consume_vmo_counter.clone();
            let finish_consume_vmo_rx = finish_consume_vmo_rx.shared();
            move |vmo| {
                let consume_vmo_counter = consume_vmo_counter.clone();
                let finish_consume_vmo_rx = finish_consume_vmo_rx.clone();
                async move {
                    assert_vmo_content!(&vmo, b"Content");

                    consume_vmo_counter.fetch_add(1, Ordering::Relaxed);
                    finish_consume_vmo_rx.await.expect("finish_consume_vmo_tx was not called.");
                    consume_vmo_counter.fetch_add(1, Ordering::Relaxed);
                }
            }
        }),
        {
            let client_counter = client_counter.clone();
            |proxy| async move {
                client_counter.fetch_add(1, Ordering::Relaxed);

                assert_write!(proxy, "Content");
                assert_close!(proxy);

                client_counter.fetch_add(1, Ordering::Relaxed);

                finish_client_rx.await.expect("finish_client_tx was not called.");
            }
        },
    )
    .coordinator(|mut controller| {
        let check_consume_vmo_client_counts = |expected_write, expected_client| {
            assert_eq!(consume_vmo_counter.load(Ordering::Relaxed), expected_write);
            assert_eq!(client_counter.load(Ordering::Relaxed), expected_client);
        };

        controller.run_until_stalled();

        // The server is waiting, but the client is done.
        check_consume_vmo_client_counts(1, 2);

        finish_consume_vmo_tx.send(()).unwrap();
        controller.run_until_stalled();

        // The server and the client are done.
        check_consume_vmo_client_counts(2, 2);

        finish_client_tx.send(()).unwrap();
    })
    .run();
}

#[test]
fn get_buffer_read_only() {
    run_server_client(
        OPEN_RIGHT_READABLE,
        simple_read_only(b"Read only test"),
        |proxy| async move {
            {
                let buffer = assert_get_buffer!(proxy, VMO_FLAG_READ);
                assert!(buffer.is_some());
                let buffer = buffer.unwrap();

                assert_eq!(buffer.size, 14);
                assert_vmo_content!(&buffer.vmo, b"Read only test");
            }

            {
                let buffer = assert_get_buffer!(proxy, VMO_FLAG_READ | VMO_FLAG_EXACT);
                assert!(buffer.is_some());
                let buffer = buffer.unwrap();

                assert_eq!(buffer.size, 14);
                assert_vmo_content!(&buffer.vmo, b"Read only test");
            }

            {
                let buffer = assert_get_buffer!(proxy, VMO_FLAG_READ | VMO_FLAG_PRIVATE);
                assert!(buffer.is_some());
                let buffer = buffer.unwrap();

                assert_eq!(buffer.size, 14);
                assert_vmo_content!(&buffer.vmo, b"Read only test");
            }

            assert_read!(proxy, "Read only test");
            assert_close!(proxy);
        },
    );
}

#[test]
fn get_buffer_private_is_resizable() {
    run_server_client(
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        simple_read_write(b"Private is resizable", b"Private is resizable"),
        |proxy| async move {
            let buffer = assert_get_buffer!(proxy, VMO_FLAG_WRITE | VMO_FLAG_PRIVATE);
            assert!(buffer.is_some());
            let buffer = buffer.unwrap();

            assert_eq!(buffer.size, 20);
            buffer.vmo.set_size(100).unwrap();

            buffer.vmo.write(b"Writable, but the data is private", 0).unwrap();

            assert_close!(proxy);
        },
    )
}

#[test]
fn get_buffer_write_only() {
    run_server_client(
        OPEN_RIGHT_WRITABLE,
        simple_write_only(b"No shared writable VMOs"),
        |proxy| {
            async move {
                // VMO is not resizable and can not be shared in writeable mode.
                assert_write!(proxy, "No shared writable VMOs");

                {
                    let buffer = assert_get_buffer!(proxy, VMO_FLAG_WRITE);
                    assert!(buffer.is_some());
                    let buffer = buffer.unwrap();

                    assert_eq!(buffer.size, 23);
                    buffer.vmo.write(b"Default is private", 0).unwrap();
                }

                assert_get_buffer_err!(
                    proxy,
                    VMO_FLAG_WRITE | VMO_FLAG_EXACT,
                    Status::NOT_SUPPORTED
                );

                {
                    let buffer = assert_get_buffer!(proxy, VMO_FLAG_WRITE | VMO_FLAG_PRIVATE);
                    assert!(buffer.is_some());
                    let buffer = buffer.unwrap();

                    assert_eq!(buffer.size, 23);
                    buffer.vmo.write(b"Private is not shared", 0).unwrap();
                }

                assert_close!(proxy);
            }
        },
    );
}

#[test]
fn get_buffer_read_write() {
    run_server_client(
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        simple_read_write(b"Initial", b"No shared writable VMOs"),
        |proxy| {
            async move {
                {
                    let buffer = assert_get_buffer!(proxy, VMO_FLAG_READ | VMO_FLAG_WRITE);
                    assert!(buffer.is_some());
                    let buffer = buffer.unwrap();

                    assert_eq!(buffer.size, 7);
                    assert_vmo_content!(&buffer.vmo, b"Initial");

                    // VMO can be updated only via the file interface.  There is no writable sharing
                    // via VMOs.
                    assert_write!(proxy, "No shared");

                    assert_vmo_content!(&buffer.vmo, b"Initial");
                }

                assert_get_buffer_err!(
                    proxy,
                    VMO_FLAG_WRITE | VMO_FLAG_EXACT,
                    Status::NOT_SUPPORTED
                );

                {
                    let buffer = assert_get_buffer!(
                        proxy,
                        VMO_FLAG_READ | VMO_FLAG_WRITE | VMO_FLAG_PRIVATE
                    );
                    assert!(buffer.is_some());
                    let buffer = buffer.unwrap();

                    assert_eq!(buffer.size, 9);
                    assert_vmo_content!(&buffer.vmo, b"No shared");
                    buffer.vmo.write(b"-Private-", 0).unwrap();

                    // VMO can be updated only via the file interface.  There is no writable sharing
                    // via VMOs.
                    assert_write!(proxy, " writable VMOs");

                    assert_vmo_content!(&buffer.vmo, b"-Private-");
                }

                assert_close!(proxy);
            }
        },
    );
}

#[test]
fn get_buffer_two_vmos() {
    run_server_client(
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        simple_read_write(b"Initial", b"Updated content"),
        //                  0           0         1
        //                  0123456     012345678901234
        |proxy| async move {
            let buffer1 = assert_get_buffer!(proxy, VMO_FLAG_READ | VMO_FLAG_EXACT);
            assert!(buffer1.is_some());
            let buffer1 = buffer1.unwrap();

            assert_eq!(buffer1.size, 7);
            assert_vmo_content!(&buffer1.vmo, b"Initial");

            assert_write!(proxy, "Updated content");

            let buffer2 = assert_get_buffer!(proxy, VMO_FLAG_READ | VMO_FLAG_EXACT);
            assert!(buffer2.is_some());
            let buffer2 = buffer2.unwrap();

            assert_eq!(buffer2.size, 15);
            assert_vmo_content!(&buffer2.vmo, b"Updated content");
            assert_vmo_content!(&buffer1.vmo, b"Updated content");

            assert_close!(proxy);
        },
    );
}
