// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests for the lazy directory.

use super::{lazy, lazy_with_watchers, LazyDirectory, WatcherEvent};

// Macros are exported into the root of the crate.
use crate::{
    assert_channel_closed, assert_close, assert_event, assert_get_token, assert_link, assert_read,
    assert_read_dirents, assert_read_dirents_err, open_get_directory_proxy_assert_ok,
    open_get_file_proxy_assert_ok, open_get_proxy_assert,
};

use crate::{
    directory::{
        dirents_sink::{self, AppendResult},
        entry::{DirectoryEntry, EntryInfo},
        test_utils::{run_server_client, test_server_client, DirentsSameInodeBuilder},
        traversal_position::TraversalPosition::{self, End, Name, Start},
    },
    execution_scope::ExecutionScope,
    file::pcb::asynchronous::{read_only, read_only_static},
    registry::token_registry,
};

use {
    async_trait::async_trait,
    fidl_fuchsia_io::{
        DIRENT_TYPE_DIRECTORY, DIRENT_TYPE_FILE, INO_UNKNOWN, OPEN_FLAG_DESCRIBE,
        OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE, WATCH_MASK_ADDED, WATCH_MASK_EXISTING,
        WATCH_MASK_IDLE, WATCH_MASK_REMOVED,
    },
    fuchsia_async::Executor,
    fuchsia_zircon::Status,
    futures::{
        channel::mpsc,
        future::{self, BoxFuture},
        lock::Mutex,
    },
    proc_macro_hack::proc_macro_hack,
    std::{
        marker::{Send, Sync},
        sync::{
            atomic::{AtomicU8, Ordering},
            Arc,
        },
    },
};

// Create level import of this macro does not affect nested modules.  And as attributes can
// only be applied to the whole "use" directive, this need to be present here and need to be
// separate form the above.  "use crate::pseudo_directory" generates a warning referring to
// "issue #52234 <https://github.com/rust-lang/rust/issues/52234>".
#[proc_macro_hack(support_nested)]
use vfs_macros::{mut_pseudo_directory, pseudo_directory};

struct Entries {
    entries: Box<[(TraversalPosition, EntryInfo)]>,
    get_entry_fn:
        Box<dyn Fn(String) -> Result<Arc<dyn DirectoryEntry>, Status> + Send + Sync + 'static>,
}

fn not_found(_name: String) -> Result<Arc<dyn DirectoryEntry>, Status> {
    Err(Status::NOT_FOUND)
}

const DOT: (u8, &'static str) = (DIRENT_TYPE_DIRECTORY, ".");

impl Entries {
    fn new<F: Fn(String) -> Result<Arc<dyn DirectoryEntry>, Status> + Send + Sync + 'static>(
        mut entries: Vec<(u8, &'static str)>,
        get_entry_fn: F,
    ) -> Self {
        entries.sort_unstable_by_key(|&(_, name)| name);
        let entries = entries
            .into_iter()
            .map(|(dirent_type, name)| {
                let pos = if name == "." {
                    TraversalPosition::Start
                } else {
                    TraversalPosition::Name(name.to_string())
                };
                (pos, EntryInfo::new(INO_UNKNOWN, dirent_type))
            })
            .collect::<Box<[(TraversalPosition, EntryInfo)]>>();
        Entries { entries, get_entry_fn: Box::new(get_entry_fn) }
    }
}

#[async_trait]
impl LazyDirectory for Entries {
    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        mut sink: Box<dyn dirents_sink::Sink>,
    ) -> Result<(TraversalPosition, Box<dyn dirents_sink::Sealed>), Status> {
        let candidate = self.entries.binary_search_by(|(entry_pos, _)| entry_pos.cmp(pos));
        let mut i = match candidate {
            Ok(i) => i,
            Err(i) => i,
        };

        while i < self.entries.len() {
            let (entry_pos, entry_info) = &self.entries[i];
            let name = match entry_pos {
                Start => ".",
                Name(name) => name,
                End => panic!("`entries` does not contain End"),
                _ => unreachable!(),
            };

            sink = match sink.append(&entry_info, name) {
                AppendResult::Ok(sink) => sink,
                AppendResult::Sealed(done) => {
                    return Ok((entry_pos.clone(), done));
                }
            };

            i += 1;
        }

        Ok((TraversalPosition::End, sink.seal()))
    }

    async fn get_entry(&self, name: String) -> Result<Arc<dyn DirectoryEntry>, Status> {
        (self.get_entry_fn)(name)
    }
}

#[test]
fn empty() {
    run_server_client(
        OPEN_RIGHT_READABLE,
        lazy(Entries::new(vec![], not_found)),
        |root| async move {
            assert_close!(root);
        },
    );
}

#[test]
fn empty_with_watchers() {
    let (mut watcher_events, watcher_events_consumer) = mpsc::unbounded();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::new();

    let server =
        lazy_with_watchers(scope.clone(), Entries::new(vec![], not_found), watcher_events_consumer);

    test_server_client(OPEN_RIGHT_READABLE, server, |root| async move {
        assert_close!(root);
        watcher_events.disconnect();
    })
    .exec(exec)
    .run();
}

#[test]
fn static_listing() {
    let entries = vec![
        DOT,
        (DIRENT_TYPE_FILE, "one"),
        (DIRENT_TYPE_FILE, "two"),
        (DIRENT_TYPE_FILE, "three"),
    ];

    run_server_client(OPEN_RIGHT_READABLE, lazy(Entries::new(entries, not_found)), |root| {
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
    });
}

#[test]
fn static_entries() {
    let entries = vec![
        DOT,
        (DIRENT_TYPE_FILE, "one"),
        (DIRENT_TYPE_FILE, "two"),
        (DIRENT_TYPE_FILE, "three"),
    ];

    let get_entry = |name: String| {
        Ok(read_only(move || {
            let name = name.clone();
            async move {
                let content = format!("File {} content", name);
                Ok(content.into_bytes())
            }
        }) as Arc<dyn DirectoryEntry>)
    };

    run_server_client(
        OPEN_RIGHT_READABLE,
        lazy(Entries::new(entries, get_entry)),
        |root| async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            open_as_file_assert_content!(&root, flags, "one", "File one content");
            open_as_file_assert_content!(&root, flags, "two", "File two content");
            open_as_file_assert_content!(&root, flags, "three", "File three content");

            assert_close!(root);
        },
    );
}

#[test]
fn static_entries_with_traversal() {
    let entries = vec![DOT, (DIRENT_TYPE_DIRECTORY, "etc"), (DIRENT_TYPE_FILE, "files")];

    let get_entry = |name: String| match &*name {
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
    };

    run_server_client(
        OPEN_RIGHT_READABLE,
        lazy(Entries::new(entries, get_entry)),
        |root| async move {
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
        },
    );
}

// DynamicEntries is a helper that will return a different set of entries for each iteration.
struct DynamicEntries {
    entries: Box<[Entries]>,
    index: Mutex<usize>,
}

impl DynamicEntries {
    fn new(entries: Box<[Entries]>) -> Self {
        DynamicEntries { entries, index: Mutex::new(0) }
    }
}

#[async_trait]
impl LazyDirectory for DynamicEntries {
    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        sink: Box<dyn dirents_sink::Sink>,
    ) -> Result<(TraversalPosition, Box<dyn dirents_sink::Sealed>), Status> {
        let mut index = self.index.lock().await;
        let result = self.entries[*index].read_dirents(pos, sink).await;
        // If we finished an iteration, move on to the next set of entries.
        if let Ok((TraversalPosition::End, _)) = result {
            if *index + 1 < self.entries.len() {
                *index += 1;
            }
        }
        result
    }

    async fn get_entry(&self, _name: String) -> Result<Arc<dyn DirectoryEntry>, Status> {
        Err(Status::NOT_FOUND)
    }
}

#[test]
fn dynamic_listing() {
    let listing1 = vec![DOT, (DIRENT_TYPE_FILE, "one"), (DIRENT_TYPE_FILE, "two")];
    let listing2 = vec![DOT, (DIRENT_TYPE_FILE, "two"), (DIRENT_TYPE_FILE, "three")];

    let entries = DynamicEntries::new(
        vec![Entries::new(listing1, not_found), Entries::new(listing2, not_found)].into(),
    );

    run_server_client(OPEN_RIGHT_READABLE, lazy(entries), |root| {
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
    });
}

#[test]
fn dynamic_entries() {
    let entries = vec![DOT, (DIRENT_TYPE_FILE, "file1"), (DIRENT_TYPE_FILE, "file2")];

    let count = Arc::new(AtomicU8::new(0));
    let get_entry = move |name: String| {
        let entry = |count: u8| {
            Ok(read_only(move || async move {
                let content = format!("Content: {}", count);
                Ok(content.into_bytes())
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
    };

    run_server_client(
        OPEN_RIGHT_READABLE,
        lazy(Entries::new(entries, get_entry)),
        |root| async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

            open_as_file_assert_content!(&root, flags, "file1", "Content: 1");
            open_as_file_assert_content!(&root, flags, "file1", "Content: 2");
            open_as_file_assert_content!(&root, flags, "file2", "Content: 12");
            open_as_file_assert_content!(&root, flags, "file2", "Content: 22");
            open_as_file_assert_content!(&root, flags, "file1", "Content: 23");

            assert_close!(root);
        },
    );
}

#[test]
fn read_dirents_small_buffer() {
    let entries = vec![
        DOT,
        (DIRENT_TYPE_DIRECTORY, "etc"),
        (DIRENT_TYPE_FILE, "files"),
        (DIRENT_TYPE_FILE, "more"),
        (DIRENT_TYPE_FILE, "uname"),
    ];

    run_server_client(OPEN_RIGHT_READABLE, lazy(Entries::new(entries, not_found)), |root| {
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
    });
}

#[test]
fn read_dirents_very_small_buffer() {
    let entries = vec![DOT, (DIRENT_TYPE_FILE, "file")];

    run_server_client(OPEN_RIGHT_READABLE, lazy(Entries::new(entries, not_found)), |root| {
        async move {
            // Entry header is 10 bytes, so this read should not be able to return a single
            // entry.
            assert_read_dirents_err!(root, 8, Status::BUFFER_TOO_SMALL);

            {
                let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                expected.add(DIRENT_TYPE_DIRECTORY, b".").add(DIRENT_TYPE_FILE, b"file");
                assert_read_dirents!(root, 100, expected.into_vec());
            }

            assert_close!(root);
        }
    });
}

#[test]
fn watch_empty() {
    let (_watcher_sender, watcher_stream) = mpsc::unbounded::<WatcherEvent>();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::new();

    let root = lazy_with_watchers(scope.clone(), Entries::new(vec![], not_found), watcher_stream);
    test_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
        let mask = WATCH_MASK_EXISTING | WATCH_MASK_IDLE | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
        let watcher_client = assert_watch!(root, mask);

        assert_watcher_one_message_watched_events!(watcher_client, { IDLE, vec![] });

        drop(watcher_client);
        assert_close!(root);
    })
    .exec(exec)
    .run();
}

#[test]
fn watch_non_empty() {
    let entries = vec![
        DOT,
        (DIRENT_TYPE_FILE, "one"),
        (DIRENT_TYPE_FILE, "two"),
        (DIRENT_TYPE_FILE, "three"),
    ];
    let (_watcher_sender, watcher_stream) = mpsc::unbounded::<WatcherEvent>();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::new();

    let root = lazy_with_watchers(scope.clone(), Entries::new(entries, not_found), watcher_stream);

    test_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
        let mask = WATCH_MASK_EXISTING | WATCH_MASK_IDLE | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
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
    })
    .exec(exec)
    .run();
}

#[test]
fn watch_two_watchers() {
    let entries = vec![
        DOT,
        (DIRENT_TYPE_FILE, "one"),
        (DIRENT_TYPE_FILE, "two"),
        (DIRENT_TYPE_FILE, "three"),
    ];
    let (_watcher_sender, watcher_stream) = mpsc::unbounded::<WatcherEvent>();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::new();

    let root = lazy_with_watchers(scope.clone(), Entries::new(entries, not_found), watcher_stream);

    test_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
        let mask = WATCH_MASK_EXISTING | WATCH_MASK_IDLE | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
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
    })
    .exec(exec)
    .run();
}

#[test]
fn watch_with_mask() {
    let entries = vec![
        DOT,
        (DIRENT_TYPE_FILE, "one"),
        (DIRENT_TYPE_FILE, "two"),
        (DIRENT_TYPE_FILE, "three"),
    ];
    let (_watcher_sender, watcher_stream) = mpsc::unbounded::<WatcherEvent>();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::new();

    let root = lazy_with_watchers(scope.clone(), Entries::new(entries, not_found), watcher_stream);

    test_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
        let mask = WATCH_MASK_IDLE | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
        let watcher_client = assert_watch!(root, mask);

        assert_watcher_one_message_watched_events!(watcher_client, { IDLE, vec![] });

        drop(watcher_client);
        assert_close!(root);
    })
    .exec(exec)
    .run();
}

#[test]
fn watch_addition() {
    let entries = vec![DOT, (DIRENT_TYPE_FILE, "one")];

    let (watcher_sender, watcher_stream) = mpsc::unbounded::<WatcherEvent>();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::new();

    let root = lazy_with_watchers(scope.clone(), Entries::new(entries, not_found), watcher_stream);

    test_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
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
    })
    .exec(exec)
    .run();
}

#[test]
fn watch_removal() {
    let entries = vec![
        DOT,
        (DIRENT_TYPE_FILE, "one"),
        (DIRENT_TYPE_FILE, "two"),
        (DIRENT_TYPE_FILE, "three"),
        (DIRENT_TYPE_FILE, "four"),
    ];

    let (watcher_sender, watcher_stream) = mpsc::unbounded::<WatcherEvent>();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::new();

    let root = lazy_with_watchers(scope.clone(), Entries::new(entries, not_found), watcher_stream);

    test_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
        let mask = WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
        let watcher_client = assert_watch!(root, mask);

        watcher_sender
            .unbounded_send(WatcherEvent::Removed(vec!["two".to_string()]))
            .expect("watcher_sender.send() failed");

        assert_watcher_one_message_watched_events!(watcher_client, { REMOVED, "two" });

        watcher_sender
            .unbounded_send(WatcherEvent::Removed(vec!["three".to_string(), "four".to_string()]))
            .expect("watcher_sender.send() failed");

        assert_watcher_one_message_watched_events!(
            watcher_client,
            { REMOVED, "three" },
            { REMOVED, "four" },
        );

        assert_close!(root);
    })
    .exec(exec)
    .run();
}

#[test]
fn watch_watcher_stream_closed() {
    let entries = vec![
        DOT,
        (DIRENT_TYPE_FILE, "one"),
        (DIRENT_TYPE_FILE, "two"),
        (DIRENT_TYPE_FILE, "three"),
    ];
    // Dropping the sender will close the receiver end.
    let (_, watcher_stream) = mpsc::unbounded::<WatcherEvent>();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::new();

    let root = lazy_with_watchers(scope.clone(), Entries::new(entries, not_found), watcher_stream);

    test_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
        let mask = WATCH_MASK_EXISTING | WATCH_MASK_IDLE;
        assert_watch_err!(root, mask, Status::NOT_SUPPORTED);

        assert_close!(root);
    })
    .exec(exec)
    .run();
}

#[test]
fn watch_close_watcher_stream() {
    let entries = vec![
        DOT,
        (DIRENT_TYPE_FILE, "one"),
        (DIRENT_TYPE_FILE, "two"),
        (DIRENT_TYPE_FILE, "three"),
    ];
    let (watcher_sender, watcher_stream) = mpsc::unbounded::<WatcherEvent>();

    let exec = Executor::new().expect("Executor creation failed");
    let scope = ExecutionScope::new();

    let root = lazy_with_watchers(scope.clone(), Entries::new(entries, not_found), watcher_stream);

    test_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
        let mask = WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
        let watcher_client = assert_watch!(root, mask);

        watcher_sender
            .unbounded_send(WatcherEvent::Added(vec!["four".to_string()]))
            .expect("watcher_sender.send() failed");

        assert_watcher_one_message_watched_events!(watcher_client, { ADDED, "four" });

        watcher_sender.close_channel();

        assert_channel_closed!(watcher_client);
        assert_close!(root);
    })
    .exec(exec)
    .run();
}

#[test]
fn link_from_lazy_into_mutable() {
    let entries = vec![DOT, (DIRENT_TYPE_FILE, "passwd")];

    let count = Arc::new(AtomicU8::new(0));
    let get_entry = move |name: String| {
        assert_eq!(name, "passwd");

        let count = count.fetch_add(1, Ordering::Relaxed) + 1;
        Ok(read_only(move || async move {
            let content = format!("Connection {}", count);
            Ok(content.into_bytes())
        }) as Arc<dyn DirectoryEntry>)
    };

    let root = pseudo_directory! {
        "etc" => lazy(Entries::new(entries, get_entry)),
        "tmp" => mut_pseudo_directory! {}
    };

    test_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| async move {
        let ro_flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
        let rw_flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;

        let etc = open_get_directory_proxy_assert_ok!(&proxy, ro_flags, "etc");
        let tmp = open_get_directory_proxy_assert_ok!(&proxy, rw_flags, "tmp");

        let tmp_watcher_client = {
            let mask = WATCH_MASK_EXISTING | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
            let watcher_client = assert_watch!(tmp, mask);
            assert_watcher_one_message_watched_events!(watcher_client, { EXISTING, "." });
            watcher_client
        };

        open_as_file_assert_content!(&etc, ro_flags, "passwd", "Connection 1");

        let tmp_token = assert_get_token!(&tmp);
        assert_link!(&etc, "passwd", tmp_token, "linked-passwd");

        assert_watcher_one_message_watched_events!(
            tmp_watcher_client,
            { ADDED, "linked-passwd" },
        );

        {
            let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
            expected.add(DIRENT_TYPE_DIRECTORY, b".").add(DIRENT_TYPE_FILE, b"passwd");

            assert_read_dirents!(etc, 1000, expected.into_vec());
        }

        {
            let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
            expected.add(DIRENT_TYPE_DIRECTORY, b".").add(DIRENT_TYPE_FILE, b"linked-passwd");

            assert_read_dirents!(tmp, 1000, expected.into_vec());
        }

        let linked_passwd = open_get_file_proxy_assert_ok!(&tmp, ro_flags, "linked-passwd");
        assert_read!(linked_passwd, "Connection 2");

        open_as_file_assert_content!(&etc, ro_flags, "passwd", "Connection 3");

        drop(tmp_watcher_client);
        assert_close!(linked_passwd);
        assert_close!(tmp);
        assert_close!(etc);
        assert_close!(proxy);
    })
    .token_registry(token_registry::Simple::new())
    .run();
}
