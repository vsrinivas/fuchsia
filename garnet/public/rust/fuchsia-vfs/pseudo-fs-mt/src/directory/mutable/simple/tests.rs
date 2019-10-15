// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests for the mutable simple directory.
//!
//! As both mutable and immutable simple directories share the implementation, there is very little
//! chance that the use cases covered by the unit tests for the immutable simple directory will
//! fail for the mutable case.  So, this suite focuses on the mutable test cases.

use super::simple;

// Macros are exported into the root of the crate.
use crate::{
    assert_close, assert_event, assert_get_token, assert_get_token_err, assert_link,
    assert_link_err, assert_read, assert_read_dirents, assert_rename, assert_rename_err,
    assert_unlink, assert_unlink_err, assert_watch, assert_watcher_one_message_watched_events,
    assert_write, open_as_file_assert_err, open_get_directory_proxy_assert_ok,
    open_get_file_proxy_assert_ok, open_get_proxy_assert,
};

use crate::{
    directory::test_utils::{run_server_client, test_server_client, DirentsSameInodeBuilder},
    file::{pcb::asynchronous::read_only_static, vmo::asynchronous::test_utils::simple_read_write},
    registry::token_registry,
};

use {
    fidl_fuchsia_io::{
        DIRENT_TYPE_DIRECTORY, DIRENT_TYPE_FILE, INO_UNKNOWN, OPEN_FLAG_DESCRIBE,
        OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE, WATCH_MASK_ADDED, WATCH_MASK_EXISTING,
        WATCH_MASK_REMOVED,
    },
    proc_macro_hack::proc_macro_hack,
};

// Create level import of this macro does not affect nested modules.  And as attributes can
// only be applied to the whole "use" directive, this need to be present here and need to be
// separate form the above.  "use crate::pseudo_directory" generates a warning referring to
// "issue #52234 <https://github.com/rust-lang/rust/issues/52234>".
#[proc_macro_hack(support_nested)]
use fuchsia_vfs_pseudo_fs_mt_macros::{mut_pseudo_directory, pseudo_directory};

#[test]
fn empty_directory() {
    run_server_client(OPEN_RIGHT_READABLE, simple(), |proxy| {
        async move {
            assert_close!(proxy);
        }
    });
}

#[test]
fn unlink_entry() {
    let root = mut_pseudo_directory! {
        "fstab" => read_only_static(b"/dev/fs /"),
        "passwd" => read_only_static(b"[redacted]"),
    };

    run_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| {
        async move {
            let ro_flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

            open_as_file_assert_content!(&proxy, ro_flags, "fstab", "/dev/fs /");
            open_as_file_assert_content!(&proxy, ro_flags, "passwd", "[redacted]");

            assert_unlink!(&proxy, "passwd");

            open_as_file_assert_content!(&proxy, ro_flags, "fstab", "/dev/fs /");
            open_as_file_assert_err!(&proxy, ro_flags, "passwd", Status::NOT_FOUND);

            assert_close!(proxy);
        }
    });
}

#[test]
fn unlink_absent_entry() {
    let root = mut_pseudo_directory! {
        "fstab" => read_only_static(b"/dev/fs /"),
    };

    run_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| {
        async move {
            let ro_flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

            open_as_file_assert_content!(&proxy, ro_flags, "fstab", "/dev/fs /");

            assert_unlink_err!(&proxy, "fstab.2", Status::NOT_FOUND);

            open_as_file_assert_content!(&proxy, ro_flags, "fstab", "/dev/fs /");

            assert_close!(proxy);
        }
    });
}

#[test]
fn unlink_does_not_traverse() {
    let root = mut_pseudo_directory! {
        "etc" => mut_pseudo_directory! {
            "fstab" => read_only_static(b"/dev/fs /"),
        },
    };

    run_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| {
        async move {
            let ro_flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

            open_as_file_assert_content!(&proxy, ro_flags, "etc/fstab", "/dev/fs /");

            assert_unlink_err!(&proxy, "etc/fstab", Status::BAD_PATH);

            open_as_file_assert_content!(&proxy, ro_flags, "etc/fstab", "/dev/fs /");

            assert_close!(proxy);
        }
    });
}

/// Keep this test in sync with [`rename_within_directory_with_watchers`].  See there for details.
#[test]
fn rename_within_directory() {
    let root = mut_pseudo_directory! {
        "passwd" => read_only_static(b"/dev/fs /"),
    };

    test_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| {
        async move {
            let ro_flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

            open_as_file_assert_content!(&proxy, ro_flags, "passwd", "/dev/fs /");

            let root_token = assert_get_token!(&proxy);
            assert_rename!(&proxy, "passwd", root_token, "fstab");

            open_as_file_assert_err!(&proxy, ro_flags, "passwd", Status::NOT_FOUND);
            open_as_file_assert_content!(&proxy, ro_flags, "fstab", "/dev/fs /");

            assert_close!(proxy);
        }
    })
    .token_registry(token_registry::Simple::new())
    .run();
}

/// Keep this test in sync with [`rename_across_directories_with_watchers`].  See there for
/// details.
#[test]
fn rename_across_directories() {
    let root = mut_pseudo_directory! {
        "tmp" => mut_pseudo_directory! {
            "fstab.new" => read_only_static(b"/dev/fs /"),
        },
        "etc" => mut_pseudo_directory! {},
    };

    test_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| {
        async move {
            let ro_flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            let rw_flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;

            open_as_file_assert_content!(&proxy, ro_flags, "tmp/fstab.new", "/dev/fs /");
            open_as_file_assert_err!(&proxy, ro_flags, "etc/fstab", Status::NOT_FOUND);

            let tmp = open_get_directory_proxy_assert_ok!(&proxy, rw_flags, "tmp");

            let etc_token = {
                let etc = open_get_directory_proxy_assert_ok!(&proxy, rw_flags, "etc");
                let token = assert_get_token!(&etc);
                assert_close!(etc);
                token
            };

            assert_rename!(&tmp, "fstab.new", etc_token, "fstab");

            open_as_file_assert_err!(&proxy, ro_flags, "tmp/fstab.new", Status::NOT_FOUND);
            open_as_file_assert_content!(&proxy, ro_flags, "etc/fstab", "/dev/fs /");

            assert_close!(tmp);
            assert_close!(proxy);
        }
    })
    .token_registry(token_registry::Simple::new())
    .run();
}

/// As the renaming code is different depending on the relative order of the directory nodes in
/// memory we need to test both directions.  But as the relative order will change on every run
/// depending on the allocation addresses, one way to test both directions is to rename back and
/// forth.
///
/// Keep this test in sync with [`rename_across_directories_twice_with_watchers`].  See there for
/// details.
#[test]
fn rename_across_directories_twice() {
    let root = mut_pseudo_directory! {
        "etc" => mut_pseudo_directory! {
            "fstab" => read_only_static(b"/dev/fs /"),
        },
        "tmp" => mut_pseudo_directory! {},
    };

    test_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| {
        async move {
            let ro_flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            let rw_flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;

            open_as_file_assert_content!(&proxy, ro_flags, "etc/fstab", "/dev/fs /");
            open_as_file_assert_err!(&proxy, ro_flags, "tmp/fstab.to-edit", Status::NOT_FOUND);

            let etc = open_get_directory_proxy_assert_ok!(&proxy, rw_flags, "etc");
            let tmp = open_get_directory_proxy_assert_ok!(&proxy, rw_flags, "tmp");

            let etc_token = assert_get_token!(&etc);
            let tmp_token = assert_get_token!(&tmp);

            assert_rename!(&etc, "fstab", tmp_token, "fstab.to-edit");

            open_as_file_assert_err!(&proxy, ro_flags, "etc/fstab", Status::NOT_FOUND);
            open_as_file_assert_content!(&proxy, ro_flags, "tmp/fstab.to-edit", "/dev/fs /");

            assert_rename!(&tmp, "fstab.to-edit", etc_token, "fstab.updated");

            open_as_file_assert_content!(&proxy, ro_flags, "etc/fstab.updated", "/dev/fs /");
            open_as_file_assert_err!(&proxy, ro_flags, "tmp/fstab.to-edit", Status::NOT_FOUND);

            assert_close!(etc);
            assert_close!(tmp);
            assert_close!(proxy);
        }
    })
    .token_registry(token_registry::Simple::new())
    .run();
}

/// This test should be exactly the same as the [`rename_within_directory`], but with watcher
/// messages monitoring.  It should help narrow down the issue when something fails, by immediately
/// showing if it is watchers related or not.
#[test]
fn rename_within_directory_with_watchers() {
    let root = mut_pseudo_directory! {
        "passwd" => read_only_static(b"/dev/fs /"),
    };

    test_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| {
        async move {
            let watcher_client = {
                let mask = WATCH_MASK_EXISTING | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;

                let watcher_client = assert_watch!(proxy, mask);

                assert_watcher_one_message_watched_events!(
                    watcher_client,
                    { EXISTING, "." },
                    { EXISTING, "passwd" },
                );
                watcher_client
            };

            let ro_flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

            open_as_file_assert_content!(&proxy, ro_flags, "passwd", "/dev/fs /");

            let root_token = assert_get_token!(&proxy);
            assert_rename!(&proxy, "passwd", root_token, "fstab");

            assert_watcher_one_message_watched_events!(watcher_client, { REMOVED, "passwd" });
            assert_watcher_one_message_watched_events!(watcher_client, { ADDED, "fstab" });

            open_as_file_assert_err!(&proxy, ro_flags, "passwd", Status::NOT_FOUND);
            open_as_file_assert_content!(&proxy, ro_flags, "fstab", "/dev/fs /");

            drop(watcher_client);
            assert_close!(proxy);
        }
    })
    .token_registry(token_registry::Simple::new())
    .run();
}

/// This test should be exactly the same as the [`rename_across_directories`], but with watcher
/// messages monitoring.  It should help narrow down the issue when something fails, by immediately
/// showing if it is watchers related or not.
#[test]
fn rename_across_directories_with_watchers() {
    let root = mut_pseudo_directory! {
        "tmp" => mut_pseudo_directory! {
            "fstab.new" => read_only_static(b"/dev/fs /"),
        },
        "etc" => mut_pseudo_directory! {},
    };

    test_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| {
        async move {
            let ro_flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            let rw_flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;

            open_as_file_assert_content!(&proxy, ro_flags, "tmp/fstab.new", "/dev/fs /");
            open_as_file_assert_err!(&proxy, ro_flags, "etc/fstab", Status::NOT_FOUND);

            let tmp = open_get_directory_proxy_assert_ok!(&proxy, rw_flags, "tmp");
            let etc = open_get_directory_proxy_assert_ok!(&proxy, rw_flags, "etc");

            let etc_token = assert_get_token!(&etc);

            let watchers_mask = WATCH_MASK_EXISTING | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;

            let tmp_watcher = {
                let watcher = assert_watch!(tmp, watchers_mask);

                assert_watcher_one_message_watched_events!(
                    watcher,
                    { EXISTING, "." },
                    { EXISTING, "fstab.new" },
                );
                watcher
            };

            let etc_watcher = {
                let watcher = assert_watch!(etc, watchers_mask);

                assert_watcher_one_message_watched_events!(watcher, { EXISTING, "." });
                watcher
            };

            assert_rename!(&tmp, "fstab.new", etc_token, "fstab");

            assert_watcher_one_message_watched_events!(tmp_watcher, { REMOVED, "fstab.new" });
            assert_watcher_one_message_watched_events!(etc_watcher, { ADDED, "fstab" });

            open_as_file_assert_err!(&proxy, ro_flags, "tmp/fstab.new", Status::NOT_FOUND);
            open_as_file_assert_content!(&proxy, ro_flags, "etc/fstab", "/dev/fs /");

            drop(tmp_watcher);
            drop(etc_watcher);
            assert_close!(tmp);
            assert_close!(etc);
            assert_close!(proxy);
        }
    })
    .token_registry(token_registry::Simple::new())
    .run();
}

/// This test should be exactly the same as the [`rename_across_directories_twice`], but with
/// watcher messages monitoring.  It should help narrow down the issue when something fails, by
/// immediately showing if it is watchers related or not.
#[test]
fn rename_across_directories_twice_with_watchers() {
    let root = mut_pseudo_directory! {
        "etc" => mut_pseudo_directory! {
            "fstab" => read_only_static(b"/dev/fs /"),
        },
        "tmp" => mut_pseudo_directory! {},
    };

    test_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| {
        async move {
            let ro_flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            let rw_flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;

            open_as_file_assert_content!(&proxy, ro_flags, "etc/fstab", "/dev/fs /");
            open_as_file_assert_err!(&proxy, ro_flags, "tmp/fstab.to-edit", Status::NOT_FOUND);

            let etc = open_get_directory_proxy_assert_ok!(&proxy, rw_flags, "etc");
            let tmp = open_get_directory_proxy_assert_ok!(&proxy, rw_flags, "tmp");

            let etc_token = assert_get_token!(&etc);
            let tmp_token = assert_get_token!(&tmp);

            let watchers_mask = WATCH_MASK_EXISTING | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;

            let etc_watcher = {
                let watcher = assert_watch!(etc, watchers_mask);

                assert_watcher_one_message_watched_events!(
                    watcher,
                    { EXISTING, "." },
                    { EXISTING, "fstab" },
                );
                watcher
            };

            let tmp_watcher = {
                let watcher = assert_watch!(tmp, watchers_mask);

                assert_watcher_one_message_watched_events!(watcher, { EXISTING, "." });
                watcher
            };

            assert_rename!(&etc, "fstab", tmp_token, "fstab.to-edit");

            assert_watcher_one_message_watched_events!(etc_watcher, { REMOVED, "fstab" });
            assert_watcher_one_message_watched_events!(tmp_watcher, { ADDED, "fstab.to-edit" });

            open_as_file_assert_err!(&proxy, ro_flags, "etc/fstab", Status::NOT_FOUND);
            open_as_file_assert_content!(&proxy, ro_flags, "tmp/fstab.to-edit", "/dev/fs /");

            assert_rename!(&tmp, "fstab.to-edit", etc_token, "fstab.updated");

            assert_watcher_one_message_watched_events!(tmp_watcher, { REMOVED, "fstab.to-edit" });
            assert_watcher_one_message_watched_events!(etc_watcher, { ADDED, "fstab.updated" });

            open_as_file_assert_content!(&proxy, ro_flags, "etc/fstab.updated", "/dev/fs /");
            open_as_file_assert_err!(&proxy, ro_flags, "tmp/fstab.to-edit", Status::NOT_FOUND);

            drop(etc_watcher);
            drop(tmp_watcher);
            assert_close!(etc);
            assert_close!(tmp);
            assert_close!(proxy);
        }
    })
    .token_registry(token_registry::Simple::new())
    .run();
}

#[test]
fn rename_into_self_with_watchers() {
    let root = mut_pseudo_directory! {
        "passwd" => read_only_static(b"[redacted]"),
    };

    test_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| {
        async move {
            let watcher_client = {
                let mask = WATCH_MASK_EXISTING | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;

                let watcher_client = assert_watch!(proxy, mask);

                assert_watcher_one_message_watched_events!(
                    watcher_client,
                    { EXISTING, "." },
                    { EXISTING, "passwd" },
                );
                watcher_client
            };

            let ro_flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

            open_as_file_assert_content!(&proxy, ro_flags, "passwd", "[redacted]");

            let root_token = assert_get_token!(&proxy);
            assert_rename!(&proxy, "passwd", root_token, "passwd");

            assert_watcher_one_message_watched_events!(watcher_client, { REMOVED, "passwd" });
            assert_watcher_one_message_watched_events!(watcher_client, { ADDED, "passwd" });

            open_as_file_assert_content!(&proxy, ro_flags, "passwd", "[redacted]");

            drop(watcher_client);
            assert_close!(proxy);
        }
    })
    .token_registry(token_registry::Simple::new())
    .run();
}

#[test]
fn get_token_fails_for_read_only_target() {
    let root = mut_pseudo_directory! {
        "etc" => mut_pseudo_directory! {
            "passwd" => read_only_static(b"[redacted]"),
        },
    };

    test_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| {
        async move {
            let ro_flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

            let etc = open_get_directory_proxy_assert_ok!(&proxy, ro_flags, "etc");
            assert_get_token_err!(&etc, Status::ACCESS_DENIED);

            assert_close!(etc);
            assert_close!(proxy);
        }
    })
    .token_registry(token_registry::Simple::new())
    .run();
}

#[test]
fn rename_fails_for_read_only_source() {
    let root = mut_pseudo_directory! {
        "etc" => mut_pseudo_directory! {
            "fstab" => read_only_static(b"/dev/fs /"),
        },
        "tmp" => mut_pseudo_directory! {},
    };

    test_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| {
        async move {
            let ro_flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            let rw_flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;

            let etc = open_get_directory_proxy_assert_ok!(&proxy, ro_flags, "etc");

            let tmp = open_get_directory_proxy_assert_ok!(&proxy, rw_flags, "tmp");
            let tmp_token = assert_get_token!(&tmp);

            assert_rename_err!(&etc, "fstab", tmp_token, "fstab", Status::ACCESS_DENIED);

            assert_close!(etc);
            assert_close!(tmp);
            assert_close!(proxy);
        }
    })
    .token_registry(token_registry::Simple::new())
    .run();
}

#[test]
fn link_file_within_directory() {
    let root = mut_pseudo_directory! {
        "file" => simple_read_write(
            b"Initial",
            b"Updated twice",
            // 0         1
            // 123456789012
        ),
    };

    test_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| {
        async move {
            let watcher_client = {
                let mask = WATCH_MASK_EXISTING | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;

                let watcher_client = assert_watch!(proxy, mask);

                assert_watcher_one_message_watched_events!(
                    watcher_client,
                    { EXISTING, "." },
                    { EXISTING, "file" },
                );
                watcher_client
            };

            // `simple_read_write` that is used to create the VMO file is overly strict, expecting
            // every open to request both read and write permissions.
            let flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;

            // We need to obtain a connection to the file before we call
            // `open_as_file_assert_content!` as the later will immediately close the connection,
            // and if no connections exist the VMO is recycled and the assertion is invoked to
            // check if the VMO content matches the final expected state.
            let file = open_get_file_proxy_assert_ok!(&proxy, flags, "file");

            open_as_file_assert_content!(&proxy, flags, "file", "Initial");

            let root_token = assert_get_token!(&proxy);
            assert_link!(&proxy, "file", root_token, "same-file");

            assert_watcher_one_message_watched_events!(watcher_client, { ADDED, "same-file" });

            {
                let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                expected
                    .add(DIRENT_TYPE_DIRECTORY, b".")
                    .add(DIRENT_TYPE_FILE, b"file")
                    .add(DIRENT_TYPE_FILE, b"same-file");

                assert_read_dirents!(proxy, 1000, expected.into_vec());
            }

            let same_file = open_get_file_proxy_assert_ok!(&proxy, flags, "same-file");
            assert_write!(file, "------- twice");
            assert_write!(same_file, "Updated");

            open_as_file_assert_content!(&proxy, flags, "file", "Updated twice");

            drop(watcher_client);
            assert_close!(file);
            assert_close!(same_file);
            assert_close!(proxy);
        }
    })
    .token_registry(token_registry::Simple::new())
    .run();
}

#[test]
fn link_file_across_directories() {
    let root = mut_pseudo_directory! {
        "etc" => mut_pseudo_directory! {
            "passwd" => simple_read_write(
                b"secret",
                b"modified twice",
                // 0         1
                // 1234567890123
            ),
        },
        "tmp" => mut_pseudo_directory! { },
    };

    test_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| {
        async move {
            // `simple_read_write` that is used to create the VMO file is overly strict, expecting
            // every open to request both read and write permissions.
            let flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;

            let etc = open_get_directory_proxy_assert_ok!(&proxy, flags, "etc");
            let tmp = open_get_directory_proxy_assert_ok!(&proxy, flags, "tmp");

            let tmp_watcher_client = {
                let mask = WATCH_MASK_EXISTING | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
                let watcher_client = assert_watch!(tmp, mask);
                assert_watcher_one_message_watched_events!(watcher_client, { EXISTING, "." });
                watcher_client
            };

            // We need to obtain a connection to the file before we call
            // `open_as_file_assert_content!` as the later will immediately close the connection,
            // and if no connections exist the VMO is recycled and the assertion is invoked to
            // check if the VMO content matches the final expected state.
            let passwd = open_get_file_proxy_assert_ok!(&etc, flags, "passwd");

            open_as_file_assert_content!(&etc, flags, "passwd", "secret");

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

            let linked_passwd = open_get_file_proxy_assert_ok!(&tmp, flags, "linked-passwd");
            assert_write!(passwd, "-------- twice");
            assert_write!(linked_passwd, "modified");

            open_as_file_assert_content!(&etc, flags, "passwd", "modified twice");

            drop(tmp_watcher_client);
            assert_close!(passwd);
            assert_close!(linked_passwd);
            assert_close!(tmp);
            assert_close!(etc);
            assert_close!(proxy);
        }
    })
    .token_registry(token_registry::Simple::new())
    .run();
}

#[test]
fn link_to_self_triggers_watcher_added() {
    let root = mut_pseudo_directory! {
        "file" => read_only_static(b"Content"),
    };

    test_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| {
        async move {
            let watcher_client = {
                let mask = WATCH_MASK_EXISTING | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;

                let watcher_client = assert_watch!(proxy, mask);

                assert_watcher_one_message_watched_events!(
                    watcher_client,
                    { EXISTING, "." },
                    { EXISTING, "file" },
                );
                watcher_client
            };

            let ro_flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

            // We need to obtain a connection to the file before we call
            // `open_as_file_assert_content!` as the later will immediately close the connection,
            // and if no connections exist the VMO is recycled and the assertion is invoked to
            // check if the VMO content matches the final expected state.
            let file = open_get_file_proxy_assert_ok!(&proxy, ro_flags, "file");

            let root_token = assert_get_token!(&proxy);
            assert_link!(&proxy, "file", root_token, "file");

            assert_watcher_one_message_watched_events!(watcher_client, { ADDED, "file" });

            {
                let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                expected.add(DIRENT_TYPE_DIRECTORY, b".").add(DIRENT_TYPE_FILE, b"file");

                assert_read_dirents!(proxy, 1000, expected.into_vec());
            }

            drop(watcher_client);
            assert_close!(file);
            assert_close!(proxy);
        }
    })
    .token_registry(token_registry::Simple::new())
    .run();
}

#[test]
fn link_from_immutable_to_mutable() {
    let root = mut_pseudo_directory! {
        "etc" => pseudo_directory! {
            "passwd" => simple_read_write(
                b"secret",
                b"modified twice",
                // 0         1
                // 1234567890123
            ),
        },
        "tmp" => mut_pseudo_directory! { },
    };

    test_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| {
        async move {
            // `simple_read_write` that is used to create the VMO file is overly strict, expecting
            // every open to request both read and write permissions.
            let flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;

            let etc = open_get_directory_proxy_assert_ok!(&proxy, flags, "etc");
            let tmp = open_get_directory_proxy_assert_ok!(&proxy, flags, "tmp");

            let tmp_watcher_client = {
                let mask = WATCH_MASK_EXISTING | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
                let watcher_client = assert_watch!(tmp, mask);
                assert_watcher_one_message_watched_events!(watcher_client, { EXISTING, "." });
                watcher_client
            };

            // We need to obtain a connection to the file before we call
            // `open_as_file_assert_content!` as the later will immediately close the connection,
            // and if no connections exist the VMO is recycled and the assertion is invoked to
            // check if the VMO content matches the final expected state.
            let passwd = open_get_file_proxy_assert_ok!(&etc, flags, "passwd");

            open_as_file_assert_content!(&etc, flags, "passwd", "secret");

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

            let linked_passwd = open_get_file_proxy_assert_ok!(&tmp, flags, "linked-passwd");
            assert_write!(passwd, "-------- twice");
            assert_write!(linked_passwd, "modified");

            open_as_file_assert_content!(&etc, flags, "passwd", "modified twice");

            drop(tmp_watcher_client);
            assert_close!(passwd);
            assert_close!(linked_passwd);
            assert_close!(tmp);
            assert_close!(etc);
            assert_close!(proxy);
        }
    })
    .token_registry(token_registry::Simple::new())
    .run();
}

#[test]
fn can_not_hardlink_directories() {
    let root = mut_pseudo_directory! {
        "etc" => mut_pseudo_directory! {
            "fstab" => read_only_static(b"/dev/fs /"),
        },
        "tmp" => mut_pseudo_directory! {},
    };

    test_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |proxy| {
        async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;

            let tmp = open_get_directory_proxy_assert_ok!(&proxy, flags, "tmp");
            let tmp_token = assert_get_token!(&tmp);

            assert_link_err!(&proxy, "etc", tmp_token, "linked-etc", Status::NOT_FILE);

            assert_close!(tmp);
            assert_close!(proxy);
        }
    })
    .token_registry(token_registry::Simple::new())
    .run();
}
