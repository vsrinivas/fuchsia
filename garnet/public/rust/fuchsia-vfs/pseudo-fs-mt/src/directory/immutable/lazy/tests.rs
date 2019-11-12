// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests for the lazy directory.

use super::{lazy, lazy_with_watchers, WatcherEvent};

// Macros are exported into the root of the crate.
use crate::{
    assert_channel_closed, assert_close, assert_event, assert_read, assert_read_dirents,
    assert_read_dirents_err, open_get_directory_proxy_assert_ok, open_get_file_proxy_assert_ok,
    open_get_proxy_assert,
};

use crate::{
    directory::{
        dirents_sink,
        entry::{DirectoryEntry, EntryInfo},
        test_utils::{run_server_client, test_server_client, DirentsSameInodeBuilder},
        traversal_position::AlphabeticalTraversal,
    },
    execution_scope::ExecutionScope,
    file::pcb::asynchronous::{read_only, read_only_static},
};

use {
    fidl_fuchsia_io::{
        DIRENT_TYPE_DIRECTORY, DIRENT_TYPE_FILE, INO_UNKNOWN, OPEN_FLAG_DESCRIBE,
        OPEN_RIGHT_READABLE, WATCH_MASK_ADDED, WATCH_MASK_EXISTING, WATCH_MASK_IDLE,
        WATCH_MASK_REMOVED,
    },
    fuchsia_async::Executor,
    fuchsia_zircon::Status,
    futures::{
        channel::mpsc,
        future::{self, BoxFuture},
        lock::Mutex,
    },
    proc_macro_hack::proc_macro_hack,
    std::sync::{
        atomic::{AtomicU8, Ordering},
        Arc,
    },
};

// Create level import of this macro does not affect nested modules.  And as attributes can
// only be applied to the whole "use" directive, this need to be present here and need to be
// separate form the above.  "use crate::pseudo_directory" generates a warning referring to
// "issue #52234 <https://github.com/rust-lang/rust/issues/52234>".
#[proc_macro_hack(support_nested)]
use fuchsia_vfs_pseudo_fs_mt_macros::pseudo_directory;

type AsyncGetEntryNames = BoxFuture<'static, Result<Box<dyn dirents_sink::Sealed>, Status>>;

/// A helper to generate `get_entry_names` callbacks for the lazy directories.  This helper
/// generates callbacks that return the same content every time and the entries are
/// alphabetically sorted (the later is convenient when traversal position need to be
/// remembered).
// I wish I would be able to move the impl type declaration into a where clause, but that would
// require me to add generic arguments to build_sorted_static_get_entry_names() and that breaks
// inference, as the return type is not specific enough.
//
// In other words, I would better write this function as
//
//     build_sorted_static_get_entry_names<Res>(...) -> impl Res
//     where
//         Res: FnMut(...) -> (...)
//
// but if I do that, then the variance of `Res` is incorrect - it becomes "for all", instead of
// "exists".  Meaning now the caller controlls what `Res` might be.
fn build_sorted_static_get_entry_names(
    mut entries: Vec<(u8, &'static str)>,
) -> (impl Fn(
    AlphabeticalTraversal,
    Box<dyn dirents_sink::Sink<AlphabeticalTraversal>>,
) -> AsyncGetEntryNames
        + Send
        + Sync) {
    use dirents_sink::AppendResult;
    use AlphabeticalTraversal::{Dot, End, Name};

    entries.sort_unstable_by_key(|&(_, name)| name);

    let entries = {
        let mut res = vec![(Dot, EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY))];
        res.extend(entries.into_iter().map(|(dirent_type, name)| {
            (Name(name.to_string()), EntryInfo::new(INO_UNKNOWN, dirent_type))
        }));
        res
    };

    move |start_pos, mut sink| {
        let candidate = entries.binary_search_by(|(entry_pos, _)| entry_pos.cmp(&start_pos));
        let mut i = match candidate {
            Ok(i) => i,
            Err(i) => i,
        };

        while i < entries.len() {
            let (pos, entry_info) = &entries[i];
            let name = match &pos {
                Dot => ".",
                Name(name) => name,
                End => panic!("`entries` does not contain End"),
            };

            sink = match sink.append(&entry_info, name, &|| pos.clone()) {
                AppendResult::Ok(sink) => sink,
                AppendResult::Sealed(done) => return Box::pin(async move { Ok(done) }),
            };

            i += 1;
        }

        Box::pin(async move { Ok(sink.seal(AlphabeticalTraversal::End)) })
    }
}

#[test]
fn empty() {
    run_server_client(
        OPEN_RIGHT_READABLE,
        lazy(
            |_p, sink| future::ready(Ok(sink.seal(AlphabeticalTraversal::End))),
            |_name| future::ready(Err(Status::NOT_FOUND)),
        ),
        |root| {
            async move {
                assert_close!(root);
            }
        },
    );
}

#[test]
fn empty_with_watchers() {
    let (mut watcher_events, watcher_events_consumer) = mpsc::unbounded();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::from_executor(Box::new(exec.ehandle()));

    let server = lazy_with_watchers(
        scope.clone(),
        |_p, sink| future::ready(Ok(sink.seal(AlphabeticalTraversal::End))),
        |_name| future::ready(Err(Status::NOT_FOUND)),
        watcher_events_consumer,
    );

    test_server_client(OPEN_RIGHT_READABLE, server, |root| {
        async move {
            assert_close!(root);
            watcher_events.disconnect();
        }
    })
    .exec(exec)
    .run();
}

#[test]
fn static_listing() {
    let get_entry_names = build_sorted_static_get_entry_names(vec![
        (DIRENT_TYPE_FILE, "one"),
        (DIRENT_TYPE_FILE, "two"),
        (DIRENT_TYPE_FILE, "three"),
    ]);

    run_server_client(
        OPEN_RIGHT_READABLE,
        lazy(get_entry_names, |_name| future::ready(Err(Status::NOT_FOUND))),
        |root| {
            async move {
                {
                    let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                    // Note that the build_sorted_static_get_entry_names() will sort entries
                    // alphabetically when returning them, so we see a different order here.
                    expected
                        .add(DIRENT_TYPE_DIRECTORY, b".")
                        .add(DIRENT_TYPE_FILE, b"one")
                        .add(DIRENT_TYPE_FILE, b"three")
                        .add(DIRENT_TYPE_FILE, b"two");

                    assert_read_dirents!(root, 1000, expected.into_vec());
                }

                assert_close!(root);
            }
        },
    );
}

#[test]
fn static_entries() {
    let get_entry_names = build_sorted_static_get_entry_names(vec![
        (DIRENT_TYPE_FILE, "one"),
        (DIRENT_TYPE_FILE, "two"),
        (DIRENT_TYPE_FILE, "three"),
    ]);

    let get_entry = |name: String| {
        Box::pin(async move {
            Ok(read_only(move || {
                let name = name.clone();
                async move {
                    let content = format!("File {} content", name);
                    Ok(content.into_bytes())
                }
            }) as Arc<dyn DirectoryEntry>)
        })
    };

    run_server_client(OPEN_RIGHT_READABLE, lazy(get_entry_names, get_entry), |root| {
        async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            open_as_file_assert_content!(&root, flags, "one", "File one content");
            open_as_file_assert_content!(&root, flags, "two", "File two content");
            open_as_file_assert_content!(&root, flags, "three", "File three content");

            assert_close!(root);
        }
    });
}

#[test]
fn static_entries_with_traversal() {
    let get_entry_names = build_sorted_static_get_entry_names(vec![
        (DIRENT_TYPE_DIRECTORY, "etc"),
        (DIRENT_TYPE_FILE, "files"),
    ]);

    let get_entry = |name: String| {
        Box::pin(async move {
            match &*name {
                "etc" => {
                    let etc = pseudo_directory! {
                        "fstab" => read_only_static(b"/dev/fs /"),
                        "ssh" => pseudo_directory! {
                            "sshd_config" => read_only_static(b"# Empty"),
                        },
                    };
                    Ok(etc as Arc<dyn DirectoryEntry>)
                }
                "files" => Ok(read_only_static(b"Content") as Arc<dyn DirectoryEntry>),
                _ => Err(Status::NOT_FOUND),
            }
        })
    };

    run_server_client(OPEN_RIGHT_READABLE, lazy(get_entry_names, get_entry), |root| {
        async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            {
                let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                expected
                    .add(DIRENT_TYPE_DIRECTORY, b".")
                    .add(DIRENT_TYPE_DIRECTORY, b"etc")
                    .add(DIRENT_TYPE_FILE, b"files");

                assert_read_dirents!(root, 1000, expected.into_vec());
            }

            {
                let etc_dir = open_get_directory_proxy_assert_ok!(&root, flags, "etc");

                let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                expected
                    .add(DIRENT_TYPE_DIRECTORY, b".")
                    .add(DIRENT_TYPE_FILE, b"fstab")
                    .add(DIRENT_TYPE_DIRECTORY, b"ssh");

                assert_read_dirents!(etc_dir, 1000, expected.into_vec());
                assert_close!(etc_dir);
            }

            {
                let ssh_dir = open_get_directory_proxy_assert_ok!(&root, flags, "etc/ssh");

                let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                expected.add(DIRENT_TYPE_DIRECTORY, b".").add(DIRENT_TYPE_FILE, b"sshd_config");

                assert_read_dirents!(ssh_dir, 1000, expected.into_vec());
                assert_close!(ssh_dir);
            }

            open_as_file_assert_content!(&root, flags, "etc/fstab", "/dev/fs /");
            open_as_file_assert_content!(&root, flags, "files", "Content");

            assert_close!(root);
        }
    });
}

/// This module holds a helper utility - an implementaion of a `dirents_sink` that remembers the
/// traversal position when the sink is sealed, but otherwise forwards all the operations to into a
/// wrapped sink instance.
mod pos_remembering_proxy_sink {
    use crate::directory::{
        dirents_sink::{AppendResult, Sealed, Sink},
        entry::EntryInfo,
        traversal_position::AlphabeticalTraversal,
    };

    use std::any::Any;

    pub(super) fn new(sink: Box<dyn Sink<AlphabeticalTraversal>>) -> Box<Proxy> {
        Box::new(Proxy { wrapped: AppendResult::Ok(sink), pos: Default::default() })
    }

    pub(super) struct Proxy {
        wrapped: AppendResult<AlphabeticalTraversal>,
        pos: AlphabeticalTraversal,
    }

    impl Proxy {
        pub(super) fn pos(&self) -> AlphabeticalTraversal {
            self.pos.clone()
        }

        pub(super) fn wrapped(self) -> Box<dyn Sealed> {
            match self.wrapped {
                AppendResult::Ok(_) => panic!("Sink has not been sealed"),
                AppendResult::Sealed(sealed) => sealed,
            }
        }
    }

    impl Sink<AlphabeticalTraversal> for Proxy {
        fn append(
            self: Box<Self>,
            entry: &EntryInfo,
            name: &str,
            pos: &dyn Fn() -> AlphabeticalTraversal,
        ) -> AppendResult<AlphabeticalTraversal> {
            let sink = match self.wrapped {
                AppendResult::Ok(sink) => sink,
                AppendResult::Sealed(_) => panic!("Sink has been already selaed."),
            };

            match sink.append(entry, name, pos) {
                wrapped @ AppendResult::Ok(_) => {
                    AppendResult::Ok(Box::new(Self { wrapped, pos: Default::default() }))
                }
                wrapped @ AppendResult::Sealed(_) => {
                    AppendResult::Sealed(Box::new(Self { wrapped, pos: pos() }))
                }
            }
        }

        fn seal(self: Box<Self>, pos: AlphabeticalTraversal) -> Box<dyn Sealed> {
            let sink = match self.wrapped {
                AppendResult::Ok(sink) => sink,
                AppendResult::Sealed(_) => panic!("Sink has been already selaed."),
            };

            Box::new(Self { wrapped: AppendResult::Sealed(sink.seal(pos.clone())), pos })
        }
    }

    impl Sealed for Proxy {
        fn open(self: Box<Self>) -> Box<dyn Any> {
            self
        }
    }
}

#[test]
fn dynamic_listing() {
    let listing1 = Arc::new(build_sorted_static_get_entry_names(vec![
        (DIRENT_TYPE_FILE, "one"),
        (DIRENT_TYPE_FILE, "two"),
    ]));
    let listing2 = Arc::new(build_sorted_static_get_entry_names(vec![
        (DIRENT_TYPE_FILE, "two"),
        (DIRENT_TYPE_FILE, "three"),
    ]));

    let get_entry_names = {
        #[derive(Clone)]
        enum Stage {
            One,
            Two,
        };
        let stage = Arc::new(Mutex::new(Stage::One));
        move |start_pos, sink| {
            let stage = stage.clone();
            let listing1 = listing1.clone();
            let listing2 = listing2.clone();
            async move {
                let stage = &mut *stage.lock().await;
                match stage {
                    Stage::One => {
                        let proxy = pos_remembering_proxy_sink::new(sink);
                        let proxy = listing1(start_pos, proxy)
                            .await
                            .unwrap()
                            .open()
                            .downcast::<pos_remembering_proxy_sink::Proxy>()
                            .unwrap();
                        if let AlphabeticalTraversal::End = proxy.pos() {
                            *stage = Stage::Two;
                        }
                        Ok(proxy.wrapped())
                    }
                    Stage::Two => listing2(start_pos, sink).await,
                }
            }
        }
    };

    run_server_client(
        OPEN_RIGHT_READABLE,
        lazy(get_entry_names, |_name| future::ready(Err(Status::NOT_FOUND))),
        |root| {
            async move {
                {
                    let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                    // Note that the build_sorted_static_get_entry_names() will sort entries
                    // alphabetically when returning them, so we see a different order here.
                    expected
                        .add(DIRENT_TYPE_DIRECTORY, b".")
                        .add(DIRENT_TYPE_FILE, b"one")
                        .add(DIRENT_TYPE_FILE, b"two");

                    assert_read_dirents!(root, 1000, expected.into_vec());
                }

                assert_rewind!(root);

                {
                    let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                    // Note that the build_sorted_static_get_entry_names() will sort entries
                    // alphabetically when returning them, so we see a different order here.
                    expected
                        .add(DIRENT_TYPE_DIRECTORY, b".")
                        .add(DIRENT_TYPE_FILE, b"three")
                        .add(DIRENT_TYPE_FILE, b"two");

                    assert_read_dirents!(root, 1000, expected.into_vec());
                }

                assert_close!(root);
            }
        },
    );
}

#[test]
fn dynamic_entries() {
    let get_entry_names = build_sorted_static_get_entry_names(vec![
        (DIRENT_TYPE_FILE, "file1"),
        (DIRENT_TYPE_FILE, "file2"),
    ]);

    let get_entry = {
        let count = Arc::new(AtomicU8::new(0));

        move |name: String| {
            let count = count.clone();
            async move {
                let entry = |count: u8| {
                    Ok(read_only(move || {
                        async move {
                            let content = format!("Content: {}", count);
                            Ok(content.into_bytes())
                        }
                    }) as Arc<dyn DirectoryEntry>)
                };

                match &*name {
                    "file1" => {
                        let count = count.fetch_add(1, Ordering::Relaxed) + 1;
                        entry(count)
                    }
                    "file2" => {
                        let count = count.fetch_add(10, Ordering::Relaxed) + 10;
                        entry(count)
                    }
                    _ => Err(Status::NOT_FOUND),
                }
            }
        }
    };

    run_server_client(OPEN_RIGHT_READABLE, lazy(get_entry_names, get_entry), |root| {
        async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

            open_as_file_assert_content!(&root, flags, "file1", "Content: 1");
            open_as_file_assert_content!(&root, flags, "file1", "Content: 2");
            open_as_file_assert_content!(&root, flags, "file2", "Content: 12");
            open_as_file_assert_content!(&root, flags, "file2", "Content: 22");
            open_as_file_assert_content!(&root, flags, "file1", "Content: 23");

            assert_close!(root);
        }
    });
}

#[test]
fn read_dirents_small_buffer() {
    let get_entry_names = build_sorted_static_get_entry_names(vec![
        (DIRENT_TYPE_DIRECTORY, "etc"),
        (DIRENT_TYPE_FILE, "files"),
        (DIRENT_TYPE_FILE, "more"),
        (DIRENT_TYPE_FILE, "uname"),
    ]);

    run_server_client(
        OPEN_RIGHT_READABLE,
        lazy(get_entry_names, |_name| future::ready(Err(Status::NOT_FOUND))),
        |root| {
            async move {
                {
                    let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                    // Entry header is 10 bytes + length of the name in bytes.
                    // (10 + 1) = 11
                    expected.add(DIRENT_TYPE_DIRECTORY, b".");
                    assert_read_dirents!(root, 11, expected.into_vec());
                }

                {
                    let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                    expected
                        // (10 + 3) = 13
                        .add(DIRENT_TYPE_DIRECTORY, b"etc")
                        // 13 + (10 + 5) = 28
                        .add(DIRENT_TYPE_FILE, b"files");
                    assert_read_dirents!(root, 28, expected.into_vec());
                }

                {
                    let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                    expected.add(DIRENT_TYPE_FILE, b"more").add(DIRENT_TYPE_FILE, b"uname");
                    assert_read_dirents!(root, 100, expected.into_vec());
                }

                assert_read_dirents!(root, 100, vec![]);

                assert_close!(root);
            }
        },
    );
}

#[test]
fn read_dirents_very_small_buffer() {
    let get_entry_names = build_sorted_static_get_entry_names(vec![(DIRENT_TYPE_FILE, "file")]);

    run_server_client(
        OPEN_RIGHT_READABLE,
        lazy(get_entry_names, |_name| future::ready(Err(Status::NOT_FOUND))),
        |root| {
            async move {
                // Entry header is 10 bytes, so this read should not be able to return a single entry.
                assert_read_dirents_err!(root, 8, Status::BUFFER_TOO_SMALL);

                {
                    let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                    expected.add(DIRENT_TYPE_DIRECTORY, b".").add(DIRENT_TYPE_FILE, b"file");
                    assert_read_dirents!(root, 100, expected.into_vec());
                }

                assert_close!(root);
            }
        },
    );
}

#[test]
fn watch_empty() {
    let (_watcher_sender, watcher_stream) = mpsc::unbounded::<WatcherEvent>();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::from_executor(Box::new(exec.ehandle()));

    let root = lazy_with_watchers(
        scope.clone(),
        |_p, sink| future::ready(Ok(sink.seal(AlphabeticalTraversal::End))),
        |_name| future::ready(Err(Status::NOT_FOUND)),
        watcher_stream,
    );
    test_server_client(OPEN_RIGHT_READABLE, root, |root| {
        async move {
            let mask =
                WATCH_MASK_EXISTING | WATCH_MASK_IDLE | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
            let watcher_client = assert_watch!(root, mask);

            assert_watcher_one_message_watched_events!(watcher_client, { IDLE, vec![] });

            drop(watcher_client);
            assert_close!(root);
        }
    })
    .exec(exec)
    .run();
}

#[test]
fn watch_non_empty() {
    let get_entry_names = build_sorted_static_get_entry_names(vec![
        (DIRENT_TYPE_FILE, "one"),
        (DIRENT_TYPE_FILE, "two"),
        (DIRENT_TYPE_FILE, "three"),
    ]);
    let (_watcher_sender, watcher_stream) = mpsc::unbounded::<WatcherEvent>();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::from_executor(Box::new(exec.ehandle()));

    let root = lazy_with_watchers(
        scope.clone(),
        get_entry_names,
        |_name| future::ready(Err(Status::NOT_FOUND)),
        watcher_stream,
    );

    test_server_client(OPEN_RIGHT_READABLE, root, |root| {
        async move {
            let mask =
                WATCH_MASK_EXISTING | WATCH_MASK_IDLE | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
            let watcher_client = assert_watch!(root, mask);

            assert_watcher_one_message_watched_events!(
                watcher_client,
                { EXISTING, "." },
                { EXISTING, "one" },
                { EXISTING, "three" },
                { EXISTING, "two" },
            );
            assert_watcher_one_message_watched_events!(watcher_client, { IDLE, vec![] });

            drop(watcher_client);
            assert_close!(root);
        }
    })
    .exec(exec)
    .run();
}

#[test]
fn watch_two_watchers() {
    let get_entry_names = build_sorted_static_get_entry_names(vec![
        (DIRENT_TYPE_FILE, "one"),
        (DIRENT_TYPE_FILE, "two"),
        (DIRENT_TYPE_FILE, "three"),
    ]);
    let (_watcher_sender, watcher_stream) = mpsc::unbounded::<WatcherEvent>();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::from_executor(Box::new(exec.ehandle()));

    let root = lazy_with_watchers(
        scope.clone(),
        get_entry_names,
        |_name| future::ready(Err(Status::NOT_FOUND)),
        watcher_stream,
    );

    test_server_client(OPEN_RIGHT_READABLE, root, |root| {
        async move {
            let mask =
                WATCH_MASK_EXISTING | WATCH_MASK_IDLE | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
            let watcher1_client = assert_watch!(root, mask);

            assert_watcher_one_message_watched_events!(
                watcher1_client,
                { EXISTING, "." },
                { EXISTING, "one" },
                { EXISTING, "three" },
                { EXISTING, "two" },
            );
            assert_watcher_one_message_watched_events!(watcher1_client, { IDLE, vec![] });

            let watcher2_client = assert_watch!(root, mask);

            assert_watcher_one_message_watched_events!(
                watcher2_client,
                { EXISTING, "." },
                { EXISTING, "one" },
                { EXISTING, "three" },
                { EXISTING, "two" },
            );
            assert_watcher_one_message_watched_events!(watcher2_client, { IDLE, vec![] });

            drop(watcher1_client);
            drop(watcher2_client);
            assert_close!(root);
        }
    })
    .exec(exec)
    .run();
}

#[test]
fn watch_with_mask() {
    let get_entry_names = build_sorted_static_get_entry_names(vec![
        (DIRENT_TYPE_FILE, "one"),
        (DIRENT_TYPE_FILE, "two"),
        (DIRENT_TYPE_FILE, "three"),
    ]);
    let (_watcher_sender, watcher_stream) = mpsc::unbounded::<WatcherEvent>();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::from_executor(Box::new(exec.ehandle()));

    let root = lazy_with_watchers(
        scope.clone(),
        get_entry_names,
        |_name| future::ready(Err(Status::NOT_FOUND)),
        watcher_stream,
    );

    test_server_client(OPEN_RIGHT_READABLE, root, |root| {
        async move {
            let mask = WATCH_MASK_IDLE | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
            let watcher_client = assert_watch!(root, mask);

            assert_watcher_one_message_watched_events!(watcher_client, { IDLE, vec![] });

            drop(watcher_client);
            assert_close!(root);
        }
    })
    .exec(exec)
    .run();
}

#[test]
fn watch_addition() {
    let get_entry_names = build_sorted_static_get_entry_names(vec![(DIRENT_TYPE_FILE, "one")]);

    let (watcher_sender, watcher_stream) = mpsc::unbounded::<WatcherEvent>();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::from_executor(Box::new(exec.ehandle()));

    let root = lazy_with_watchers(
        scope.clone(),
        get_entry_names,
        |_name| future::ready(Err(Status::NOT_FOUND)),
        watcher_stream,
    );

    test_server_client(OPEN_RIGHT_READABLE, root, |root| {
        async move {
            let mask = WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
            let watcher_client = assert_watch!(root, mask);

            watcher_sender
                .unbounded_send(WatcherEvent::Added(vec!["two".to_string()]))
                .expect("watcher_sender.send() failed");

            assert_watcher_one_message_watched_events!(watcher_client, { ADDED, "two" });

            watcher_sender
                .unbounded_send(WatcherEvent::Added(vec!["three".to_string(), "four".to_string()]))
                .expect("watcher_sender.send() failed");

            assert_watcher_one_message_watched_events!(
                watcher_client,
                { ADDED, "three" },
                { ADDED, "four" },
            );

            assert_close!(root);
        }
    })
    .exec(exec)
    .run();
}

#[test]
fn watch_removal() {
    let get_entry_names = build_sorted_static_get_entry_names(vec![
        (DIRENT_TYPE_FILE, "one"),
        (DIRENT_TYPE_FILE, "two"),
        (DIRENT_TYPE_FILE, "three"),
        (DIRENT_TYPE_FILE, "four"),
    ]);

    let (watcher_sender, watcher_stream) = mpsc::unbounded::<WatcherEvent>();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::from_executor(Box::new(exec.ehandle()));

    let root = lazy_with_watchers(
        scope.clone(),
        get_entry_names,
        |_name| future::ready(Err(Status::NOT_FOUND)),
        watcher_stream,
    );

    test_server_client(OPEN_RIGHT_READABLE, root, |root| {
        async move {
            let mask = WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
            let watcher_client = assert_watch!(root, mask);

            watcher_sender
                .unbounded_send(WatcherEvent::Removed(vec!["two".to_string()]))
                .expect("watcher_sender.send() failed");

            assert_watcher_one_message_watched_events!(watcher_client, { REMOVED, "two" });

            watcher_sender
                .unbounded_send(WatcherEvent::Removed(vec![
                    "three".to_string(),
                    "four".to_string(),
                ]))
                .expect("watcher_sender.send() failed");

            assert_watcher_one_message_watched_events!(
                watcher_client,
                { REMOVED, "three" },
                { REMOVED, "four" },
            );

            assert_close!(root);
        }
    })
    .exec(exec)
    .run();
}

#[test]
fn watch_watcher_stream_closed() {
    let get_entry_names = build_sorted_static_get_entry_names(vec![
        (DIRENT_TYPE_FILE, "one"),
        (DIRENT_TYPE_FILE, "two"),
        (DIRENT_TYPE_FILE, "three"),
    ]);
    // Dropping the sender will close the receiver end.
    let (_, watcher_stream) = mpsc::unbounded::<WatcherEvent>();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::from_executor(Box::new(exec.ehandle()));

    let root = lazy_with_watchers(
        scope.clone(),
        get_entry_names,
        |_name| future::ready(Err(Status::NOT_FOUND)),
        watcher_stream,
    );

    test_server_client(OPEN_RIGHT_READABLE, root, |root| {
        async move {
            let mask = WATCH_MASK_EXISTING | WATCH_MASK_IDLE;
            assert_watch_err!(root, mask, Status::NOT_SUPPORTED);

            assert_close!(root);
        }
    })
    .exec(exec)
    .run();
}

#[test]
fn watch_close_watcher_stream() {
    let get_entry_names = build_sorted_static_get_entry_names(vec![
        (DIRENT_TYPE_FILE, "one"),
        (DIRENT_TYPE_FILE, "two"),
        (DIRENT_TYPE_FILE, "three"),
    ]);
    let (watcher_sender, watcher_stream) = mpsc::unbounded::<WatcherEvent>();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::from_executor(Box::new(exec.ehandle()));

    let root = lazy_with_watchers(
        scope.clone(),
        get_entry_names,
        |_name| future::ready(Err(Status::NOT_FOUND)),
        watcher_stream,
    );

    test_server_client(OPEN_RIGHT_READABLE, root, |root| {
        async move {
            let mask = WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
            let watcher_client = assert_watch!(root, mask);

            watcher_sender
                .unbounded_send(WatcherEvent::Added(vec!["four".to_string()]))
                .expect("watcher_sender.send() failed");

            assert_watcher_one_message_watched_events!(watcher_client, { ADDED, "four" });

            watcher_sender.close_channel();

            assert_channel_closed!(watcher_client);
            assert_close!(root);
        }
    })
    .exec(exec)
    .run();
}
