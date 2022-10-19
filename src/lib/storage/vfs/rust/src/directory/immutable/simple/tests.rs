// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests for the immutable simple directory.

use super::simple;

// Macros are exported into the root of the crate.
use crate::{
    assert_channel_closed, assert_close, assert_event, assert_get_attr, assert_query, assert_read,
    assert_read_dirents, assert_read_dirents_err, assert_seek, assert_write,
    clone_as_directory_assert_err, clone_get_directory_proxy_assert_ok, clone_get_proxy_assert,
    open_as_directory_assert_err, open_as_file_assert_err, open_get_directory_proxy_assert_ok,
    open_get_proxy_assert, open_get_vmo_file_proxy_assert_ok,
};

use crate::{
    directory::{
        entry::{DirectoryEntry, EntryInfo},
        helper::DirectlyMutable,
        immutable::{simple_with_inode, Simple},
        test_utils::{run_server_client, DirentsSameInodeBuilder},
    },
    execution_scope::ExecutionScope,
    file::vmo::asynchronous::{read_only_static, read_write, simple_init_vmo_with_capacity},
    path::Path,
    test_utils::node::open_get_proxy,
    test_utils::{build_flag_combinations, run_client},
};

use {
    fidl::endpoints::{create_proxy, Proxy},
    fidl_fuchsia_io as fio,
    fuchsia_async::{self as fasync, TestExecutor},
    fuchsia_zircon::{
        sys::{self, ZX_OK},
        Status,
    },
    libc::{S_IRUSR, S_IXUSR},
    static_assertions::assert_eq_size,
    std::sync::{Arc, Mutex},
    vfs_macros::pseudo_directory,
};

#[test]
fn empty_directory() {
    run_server_client(fio::OpenFlags::RIGHT_READABLE, simple(), |root| async move {
        assert_close!(root);
    });
}

#[test]
fn empty_directory_get_attr() {
    run_server_client(fio::OpenFlags::RIGHT_READABLE, simple(), |root| async move {
        assert_get_attr!(
            root,
            fio::NodeAttributes {
                mode: fio::MODE_TYPE_DIRECTORY | S_IRUSR | S_IXUSR,
                id: fio::INO_UNKNOWN,
                content_size: 0,
                storage_size: 0,
                link_count: 1,
                creation_time: 0,
                modification_time: 0,
            }
        );
        assert_close!(root);
    });
}

#[test]
fn empty_directory_with_custom_inode_get_attr() {
    run_server_client(
        fio::OpenFlags::RIGHT_READABLE,
        simple_with_inode(12345),
        |root| async move {
            assert_get_attr!(
                root,
                fio::NodeAttributes {
                    mode: fio::MODE_TYPE_DIRECTORY | S_IRUSR | S_IXUSR,
                    id: 12345,
                    content_size: 0,
                    storage_size: 0,
                    link_count: 1,
                    creation_time: 0,
                    modification_time: 0,
                }
            );
            assert_close!(root);
        },
    );
}

#[test]
fn empty_directory_describe() {
    run_server_client(fio::OpenFlags::RIGHT_READABLE, simple(), |root| async move {
        assert_query!(root, fio::DIRECTORY_PROTOCOL_NAME);
        assert_close!(root);
    });
}

#[test]
fn open_empty_directory_with_describe() {
    let exec = TestExecutor::new().expect("TestExecutor creation failed");
    let scope = ExecutionScope::new();

    let server = simple();

    run_client(exec, || async move {
        let (root, server_end) =
            create_proxy::<fio::DirectoryMarker>().expect("Failed to create connection endpoints");

        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
        server.open(scope, flags, 0, Path::dot(), server_end.into_channel().into());

        assert_event!(root, fio::DirectoryEvent::OnOpen_ { s, info }, {
            assert_eq!(s, ZX_OK);
            assert_eq!(
                info,
                Some(Box::new(fio::NodeInfoDeprecated::Directory(fio::DirectoryObject)))
            );
        });
    });
}

#[test]
fn clone() {
    let root = pseudo_directory! {
        "file" => read_only_static(b"Content"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |first_proxy| async move {
        async fn assert_read_file(root: &fio::DirectoryProxy) {
            let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
            let file = open_get_vmo_file_proxy_assert_ok!(&root, flags, "file");

            assert_read!(file, "Content");
            assert_close!(file);
        }

        assert_read_file(&first_proxy).await;

        let second_proxy = clone_get_directory_proxy_assert_ok!(
            &first_proxy,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE
        );

        assert_read_file(&second_proxy).await;

        assert_close!(first_proxy);
        assert_close!(second_proxy);
    });
}

#[test]
fn clone_inherit_access() {
    use fidl_fuchsia_io as fio;

    let root = pseudo_directory! {
        "file" => read_only_static(b"Content"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |first_proxy| async move {
        async fn assert_read_file(root: &fio::DirectoryProxy) {
            let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
            let file = open_get_vmo_file_proxy_assert_ok!(&root, flags, "file");

            assert_read!(file, "Content");
            assert_close!(file);
        }

        assert_read_file(&first_proxy).await;

        let second_proxy = clone_get_directory_proxy_assert_ok!(
            &first_proxy,
            fio::OpenFlags::CLONE_SAME_RIGHTS | fio::OpenFlags::DESCRIBE
        );

        assert_read_file(&second_proxy).await;

        assert_close!(first_proxy);
        assert_close!(second_proxy);
    });
}

#[test]
fn clone_cannot_increase_access() {
    let root = pseudo_directory! {
        "file" => read_only_static(b"Content"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        async fn assert_read_file(root: &fio::DirectoryProxy) {
            let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
            let file = open_get_vmo_file_proxy_assert_ok!(&root, flags, "file");

            assert_read!(file, "Content");
            assert_close!(file);
        }

        assert_read_file(&root).await;

        clone_as_directory_assert_err!(
            &root,
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::DESCRIBE,
            Status::ACCESS_DENIED
        );

        assert_close!(root);
    });
}

#[test]
fn clone_cannot_use_same_rights_flag_with_any_specific_right() {
    use fidl_fuchsia_io as fio;

    let root = pseudo_directory! {
        "file" => read_only_static(b"Content"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
        let file = open_get_vmo_file_proxy_assert_ok!(&root, flags, "file");

        assert_read!(file, "Content");
        assert_close!(file);

        clone_as_directory_assert_err!(
            &root,
            fio::OpenFlags::CLONE_SAME_RIGHTS
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::DESCRIBE,
            Status::INVALID_ARGS
        );

        assert_close!(root);
    });
}

#[test]
fn one_file_open_existing() {
    let root = pseudo_directory! {
        "file" => read_only_static(b"Content"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
        let file = open_get_vmo_file_proxy_assert_ok!(&root, flags, "file");

        assert_read!(file, "Content");
        assert_close!(file);

        assert_close!(root);
    });
}

#[test]
fn one_file_open_missing_not_found_handler() {
    let root = pseudo_directory! {
        "file" => read_only_static("Content"),
    };

    let last_handler_value = Arc::new(Mutex::new(None));

    {
        let last_handler_value = last_handler_value.clone();
        root.clone().set_not_found_handler(Box::new(move |path| {
            *last_handler_value.lock().unwrap() = Some(path.to_string());
        }));
    }

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
        open_as_file_assert_err!(&root, flags, "file2", Status::NOT_FOUND);

        assert_close!(root);
        assert_eq!(Some("file2".to_string()), *last_handler_value.lock().unwrap())
    });
}

#[test]
fn one_file_open_missing() {
    let root = pseudo_directory! {
        "file" => read_only_static(b"Content"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
        open_as_file_assert_err!(&root, flags, "file2", Status::NOT_FOUND);

        assert_close!(root);
    });
}

#[test]
fn small_tree_traversal() {
    let root = pseudo_directory! {
        "etc" => pseudo_directory! {
            "fstab" => read_only_static(b"/dev/fs /"),
            "ssh" => pseudo_directory! {
                "sshd_config" => read_only_static(b"# Empty"),
            },
        },
        "uname" => read_only_static(b"Fuchsia"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        async fn open_read_close<'a>(
            from_dir: &'a fio::DirectoryProxy,
            path: &'a str,
            expected_content: &'a str,
        ) {
            let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
            let file = open_get_vmo_file_proxy_assert_ok!(&from_dir, flags, path);
            assert_read!(file, expected_content);
            assert_close!(file);
        }

        open_read_close(&root, "etc/fstab", "/dev/fs /").await;

        {
            let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
            let ssh_dir = open_get_directory_proxy_assert_ok!(&root, flags, "etc/ssh");

            open_read_close(&ssh_dir, "sshd_config", "# Empty").await;
        }

        open_read_close(&root, "etc/ssh/sshd_config", "# Empty").await;
        open_read_close(&root, "uname", "Fuchsia").await;

        assert_close!(root);
    });
}

#[test]
fn open_writable_in_subdir() {
    let root = {
        pseudo_directory! {
            "etc" => pseudo_directory! {
                "ssh" => pseudo_directory! {
                    "sshd_config" => read_write(
                        simple_init_vmo_with_capacity(&b"# Empty".to_vec(), 100)
                    )
                }
            }
        }
    };

    run_server_client(
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        root,
        |root| async move {
            async fn open_read_write_close<'a>(
                from_dir: &'a fio::DirectoryProxy,
                path: &'a str,
                expected_content: &'a str,
                new_content: &'a str,
            ) {
                let flags = fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::RIGHT_WRITABLE
                    | fio::OpenFlags::DESCRIBE;
                let file = open_get_vmo_file_proxy_assert_ok!(&from_dir, flags, path);
                assert_read!(file, expected_content);
                assert_seek!(file, 0, Start);
                assert_write!(file, new_content);
                assert_seek!(file, 0, Start);
                assert_read!(file, new_content);
                assert_close!(file);
            }

            {
                let flags = fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::RIGHT_WRITABLE
                    | fio::OpenFlags::DESCRIBE;
                let ssh_dir = open_get_directory_proxy_assert_ok!(&root, flags, "etc/ssh");

                open_read_write_close(&ssh_dir, "sshd_config", "# Empty", "Port 22").await;
            }
        },
    );
}

/// Ensures that the POSIX flags cannot be used to exceed the rights of a parent connection,
/// and that the correct rights are expanded when requested.
#[test]
fn open_subdir_with_posix_flag_rights_expansion() {
    let root = {
        pseudo_directory! {
            "etc" => pseudo_directory! {
                "ssh" => pseudo_directory! {}
            }
        }
    };

    // Combinations of flags to test the root directory with.
    let root_flag_combos = build_flag_combinations(
        fio::OpenFlags::RIGHT_READABLE.bits(),
        (fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::RIGHT_EXECUTABLE).bits(),
    )
    .into_iter()
    .map(fio::OpenFlags::from_bits_truncate)
    .collect::<Vec<_>>();

    // Combinations of flags to pass in when opening a subdirectory within the root directory.
    let subdir_flag_combos = build_flag_combinations(
        fio::OpenFlags::RIGHT_READABLE.bits(),
        (fio::OpenFlags::POSIX_WRITABLE | fio::OpenFlags::POSIX_EXECUTABLE).bits(),
    )
    .into_iter()
    .map(fio::OpenFlags::from_bits_truncate)
    .collect::<Vec<_>>();

    // Validates that POSIX flags passed when opening a subdirectory against the root directory
    // result in the correct expanded rights, and that they do not exceed those of the root.
    fn validate_expanded_rights(
        root_node_flags: fio::OpenFlags,
        subdir_open_flags: fio::OpenFlags,
        resulting_subdir_flags: fio::OpenFlags,
    ) {
        // Ensure POSIX flags were removed.
        assert!(
            !resulting_subdir_flags
                .intersects(fio::OpenFlags::POSIX_WRITABLE | fio::OpenFlags::POSIX_EXECUTABLE),
            "POSIX flags were not removed!"
        );
        // Ensure writable rights were expanded correctly.
        if subdir_open_flags.intersects(fio::OpenFlags::POSIX_WRITABLE) {
            if root_node_flags.intersects(fio::OpenFlags::RIGHT_WRITABLE) {
                assert!(
                    resulting_subdir_flags.intersects(fio::OpenFlags::RIGHT_WRITABLE),
                    "Failed to expand writable right!"
                );
            } else {
                assert!(
                    !resulting_subdir_flags.intersects(fio::OpenFlags::RIGHT_WRITABLE),
                    "Rights violation: improperly expanded writable right!"
                );
            }
        }
        // Ensure executable rights were expanded correctly.
        if subdir_open_flags.intersects(fio::OpenFlags::POSIX_EXECUTABLE) {
            if root_node_flags.intersects(fio::OpenFlags::RIGHT_EXECUTABLE) {
                assert!(
                    resulting_subdir_flags.intersects(fio::OpenFlags::RIGHT_EXECUTABLE),
                    "Failed to expand executable right!"
                );
            } else {
                assert!(
                    !resulting_subdir_flags.intersects(fio::OpenFlags::RIGHT_EXECUTABLE),
                    "Rights violation: improperly expanded executable right!"
                );
            }
        }
    }

    run_server_client(
        fio::OpenFlags::RIGHT_READABLE
            | fio::OpenFlags::RIGHT_WRITABLE
            | fio::OpenFlags::RIGHT_EXECUTABLE,
        root,
        |root| async move {
            for root_flags in root_flag_combos {
                // Clone the root directory with a restricted subset of rights.
                let root_flags = root_flags | fio::OpenFlags::DESCRIBE;
                let root_proxy = clone_get_directory_proxy_assert_ok!(&root, root_flags);
                for subdir_flags in &subdir_flag_combos {
                    // Open the subdirectory with a set of POSIX flags, and call get_flags to
                    // determine which set of rights they were expanded to.
                    let subdir_flags = fio::OpenFlags::DESCRIBE | *subdir_flags;
                    let subdir_proxy =
                        open_get_directory_proxy_assert_ok!(&root_proxy, subdir_flags, "etc/ssh");
                    let (_, resulting_subdir_flags) =
                        subdir_proxy.get_flags().await.expect("Failed to get node flags!");
                    // Ensure resulting rights on the subdirectory are expanded correctly and do
                    // not exceed those of the cloned root directory connection.
                    validate_expanded_rights(root_flags, subdir_flags, resulting_subdir_flags);
                    assert_close!(subdir_proxy);
                }
                assert_close!(root_proxy);
            }
            assert_close!(root);
        },
    );
}

#[test]
fn open_non_existing_path() {
    let root = pseudo_directory! {
        "dir" => pseudo_directory! {
            "file1" => read_only_static(b"Content 1"),
        },
        "file2" => read_only_static(b"Content 2"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
        open_as_file_assert_err!(&root, flags, "non-existing", Status::NOT_FOUND);
        open_as_file_assert_err!(&root, flags, "dir/file10", Status::NOT_FOUND);
        open_as_file_assert_err!(&root, flags, "dir/dir/file10", Status::NOT_FOUND);
        open_as_file_assert_err!(&root, flags, "dir/dir/file1", Status::NOT_FOUND);

        assert_close!(root);
    });
}

#[test]
fn open_empty_path() {
    let root = pseudo_directory! {
        "file_foo" => read_only_static(b"Content"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
        open_as_file_assert_err!(&root, flags, "", Status::INVALID_ARGS);

        assert_close!(root);
    });
}

#[test]
fn open_path_within_a_file() {
    let root = pseudo_directory! {
        "dir" => pseudo_directory! {
            "file1" => read_only_static(b"Content 1"),
        },
        "file2" => read_only_static(b"Content 2"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
        open_as_file_assert_err!(&root, flags, "file2/file1", Status::NOT_DIR);
        open_as_file_assert_err!(&root, flags, "dir/file1/file3", Status::NOT_DIR);

        assert_close!(root);
    });
}

#[test]
fn open_file_as_directory() {
    let root = pseudo_directory! {
        "dir" => pseudo_directory! {
            "file1" => read_only_static(b"Content 1"),
        },
        "file2" => read_only_static(b"Content 2"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let flags =
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE | fio::OpenFlags::DIRECTORY;
        {
            let root = open_get_proxy::<fio::FileMarker>(&root, flags, 0, "file2");
            assert_event!(root, fio::FileEvent::OnOpen_ { s, info }, {
                assert_eq!(Status::from_raw(s), Status::NOT_DIR);
                assert_eq!(info, None);
            });
        }
        {
            let root = open_get_proxy::<fio::FileMarker>(&root, flags, 0, "dir/file1");
            assert_event!(root, fio::FileEvent::OnOpen_ { s, info }, {
                assert_eq!(Status::from_raw(s), Status::NOT_DIR);
                assert_eq!(info, None);
            });
        }

        assert_close!(root);
    });
}

#[test]
fn open_directory_as_file() {
    let root = pseudo_directory! {
        "dir" => pseudo_directory! {
            "dir2" => pseudo_directory! {},
        },
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let flags = fio::OpenFlags::RIGHT_READABLE
            | fio::OpenFlags::DESCRIBE
            | fio::OpenFlags::NOT_DIRECTORY;
        {
            let root = open_get_proxy::<fio::DirectoryMarker>(&root, flags, 0, "dir");
            assert_event!(root, fio::DirectoryEvent::OnOpen_ { s, info }, {
                assert_eq!(Status::from_raw(s), Status::NOT_FILE);
                assert_eq!(info, None);
            });
        }
        {
            let root = open_get_proxy::<fio::DirectoryMarker>(&root, flags, 0, "dir/dir2");
            assert_event!(root, fio::DirectoryEvent::OnOpen_ { s, info }, {
                assert_eq!(Status::from_raw(s), Status::NOT_FILE);
                assert_eq!(info, None);
            });
        }

        assert_close!(root);
    });
}

#[test]
fn trailing_slash_means_directory() {
    let root = pseudo_directory! {
        "file" => read_only_static(b"Content"),
        "dir" => pseudo_directory! {},
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;

        open_as_file_assert_err!(&root, flags, "file/", Status::NOT_DIR);

        {
            let file = open_get_vmo_file_proxy_assert_ok!(&root, flags, "file");
            assert_read!(file, "Content");
            assert_close!(file);
        }

        {
            let sub_dir = open_get_directory_proxy_assert_ok!(&root, flags, "dir/");
            assert_close!(sub_dir);
        }

        assert_close!(root);
    });
}

#[test]
fn no_dots_in_open() {
    let root = pseudo_directory! {
        "file" => read_only_static(b"Content"),
        "dir" => pseudo_directory! {
            "dir2" => pseudo_directory! {},
        },
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
        open_as_directory_assert_err!(&root, flags, "dir/../dir2", Status::INVALID_ARGS);
        open_as_directory_assert_err!(&root, flags, "dir/./dir2", Status::INVALID_ARGS);
        open_as_directory_assert_err!(&root, flags, "./dir", Status::INVALID_ARGS);

        assert_close!(root);
    });
}

#[test]
fn no_consequtive_slashes_in_open() {
    let root = pseudo_directory! {
        "dir" => pseudo_directory! {
            "dir2" => pseudo_directory! {},
        },
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
        open_as_directory_assert_err!(&root, flags, "dir/../dir2", Status::INVALID_ARGS);
        open_as_directory_assert_err!(&root, flags, "dir/./dir2", Status::INVALID_ARGS);
        open_as_directory_assert_err!(&root, flags, "dir//dir2", Status::INVALID_ARGS);
        open_as_directory_assert_err!(&root, flags, "dir/dir2//", Status::INVALID_ARGS);
        open_as_directory_assert_err!(&root, flags, "//dir/dir2", Status::INVALID_ARGS);
        open_as_directory_assert_err!(&root, flags, "./dir", Status::INVALID_ARGS);

        assert_close!(root);
    });
}

#[test]
fn directories_restrict_nested_read_permissions() {
    let root = pseudo_directory! {
        "dir" => pseudo_directory! {
            "file" => read_only_static(b"Content"),
        },
    };

    run_server_client(fio::OpenFlags::empty(), root, |root| async move {
        open_as_file_assert_err!(
            &root,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE,
            "dir/file",
            Status::ACCESS_DENIED
        );

        assert_close!(root);
    });
}

#[test]
fn directories_restrict_nested_write_permissions() {
    let root = pseudo_directory! {
        "dir" => pseudo_directory! {
            "file" => read_write(simple_init_vmo_with_capacity(&[], 100))
        },
    };

    run_server_client(fio::OpenFlags::empty(), root, |root| async move {
        open_as_file_assert_err!(
            &root,
            fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::DESCRIBE,
            "dir/file",
            Status::ACCESS_DENIED
        );

        assert_close!(root);
    });
}

#[fasync::run_singlethreaded(test)]
async fn directories_remove_nested() {
    // Test dynamic removal of a subdirectory under another directory.
    let root = pseudo_directory! {
        "dir" => pseudo_directory! {
            "subdir" => pseudo_directory! {},   // To be removed below.
        },
    };
    let dir_entry = root.get_entry("dir").expect("Failed to get directory entry!");
    // Remove subdir from dir.
    let downcasted_dir = dir_entry.into_any().downcast::<Simple>().expect("Downcast failed!");
    downcasted_dir.remove_entry("subdir", true).expect("Failed to remove directory entry!");

    // Ensure it was actually removed.
    assert_eq!(downcasted_dir.get_entry("subdir").err(), Some(Status::NOT_FOUND));
}

#[test]
fn flag_posix_means_writable() {
    let root = {
        pseudo_directory! {
        "nested" => pseudo_directory! {
            "file" => read_write(
                    simple_init_vmo_with_capacity(&b"Content".to_vec(), 20)
                )
            }
        }
    };

    run_server_client(
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        root,
        |root| async move {
            let nested = open_get_directory_proxy_assert_ok!(
                &root,
                fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::POSIX_WRITABLE
                    | fio::OpenFlags::DESCRIBE,
                "nested"
            );

            clone_get_directory_proxy_assert_ok!(
                &nested,
                fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::RIGHT_WRITABLE
                    | fio::OpenFlags::DESCRIBE
            );

            {
                let flags = fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::RIGHT_WRITABLE
                    | fio::OpenFlags::DESCRIBE;
                let file = open_get_vmo_file_proxy_assert_ok!(&nested, flags, "file");

                assert_read!(file, "Content");
                assert_seek!(file, 0, Start);
                assert_write!(file, "New content");

                assert_close!(file);
            }

            assert_close!(nested);
            assert_close!(root);
        },
    );
}

#[test]
fn flag_posix_does_not_add_writable_to_read_only() {
    let root = pseudo_directory! {
        "nested" => pseudo_directory! {
            "file" => read_write(
                        simple_init_vmo_with_capacity(&b"Content".to_vec(), 100))
        },
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let nested = open_get_directory_proxy_assert_ok!(
            &root,
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::POSIX_WRITABLE
                | fio::OpenFlags::DESCRIBE,
            "nested"
        );

        clone_as_directory_assert_err!(
            &nested,
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::DESCRIBE,
            Status::ACCESS_DENIED
        );

        {
            let flags = fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::DESCRIBE;
            open_as_file_assert_err!(&nested, flags, "file", Status::ACCESS_DENIED);
        }

        {
            let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
            let file = open_get_vmo_file_proxy_assert_ok!(&nested, flags, "file");

            assert_read!(file, "Content");
            assert_close!(file);
        }

        assert_close!(nested);
        assert_close!(root);
    });
}

#[test]
fn read_dirents_large_buffer() {
    let root = pseudo_directory! {
        "etc" => pseudo_directory! {
            "fstab" => read_only_static(b"/dev/fs /"),
            "passwd" => read_only_static(b"[redacted]"),
            "shells" => read_only_static(b"/bin/bash"),
            "ssh" => pseudo_directory! {
                "sshd_config" => read_only_static(b"# Empty"),
            },
        },
        "files" => read_only_static(b"Content"),
        "more" => read_only_static(b"Content"),
        "uname" => read_only_static(b"Fuchsia"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        {
            let mut expected = DirentsSameInodeBuilder::new(fio::INO_UNKNOWN);
            expected
                .add(fio::DirentType::Directory, b".")
                .add(fio::DirentType::Directory, b"etc")
                .add(fio::DirentType::File, b"files")
                .add(fio::DirentType::File, b"more")
                .add(fio::DirentType::File, b"uname");

            assert_read_dirents!(root, 1000, expected.into_vec());
        }

        {
            let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
            let etc_dir = open_get_directory_proxy_assert_ok!(&root, flags, "etc");

            let mut expected = DirentsSameInodeBuilder::new(fio::INO_UNKNOWN);
            expected
                .add(fio::DirentType::Directory, b".")
                .add(fio::DirentType::File, b"fstab")
                .add(fio::DirentType::File, b"passwd")
                .add(fio::DirentType::File, b"shells")
                .add(fio::DirentType::Directory, b"ssh");

            assert_read_dirents!(etc_dir, 1000, expected.into_vec());
            assert_close!(etc_dir);
        }

        {
            let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
            let ssh_dir = open_get_directory_proxy_assert_ok!(&root, flags, "etc/ssh");

            let mut expected = DirentsSameInodeBuilder::new(fio::INO_UNKNOWN);
            expected
                .add(fio::DirentType::Directory, b".")
                .add(fio::DirentType::File, b"sshd_config");

            assert_read_dirents!(ssh_dir, 1000, expected.into_vec());
            assert_close!(ssh_dir);
        }

        assert_close!(root);
    });
}

#[test]
fn read_dirents_small_buffer() {
    let root = pseudo_directory! {
        "etc" => pseudo_directory! { },
        "files" => read_only_static(b"Content"),
        "more" => read_only_static(b"Content"),
        "uname" => read_only_static(b"Fuchsia"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| {
        async move {
            {
                let mut expected = DirentsSameInodeBuilder::new(fio::INO_UNKNOWN);
                // Entry header is 10 bytes + length of the name in bytes.
                // (10 + 1) = 11
                expected.add(fio::DirentType::Directory, b".");
                assert_read_dirents!(root, 11, expected.into_vec());
            }

            {
                let mut expected = DirentsSameInodeBuilder::new(fio::INO_UNKNOWN);
                expected
                    // (10 + 3) = 13
                    .add(fio::DirentType::Directory, b"etc")
                    // 13 + (10 + 5) = 28
                    .add(fio::DirentType::File, b"files");
                assert_read_dirents!(root, 28, expected.into_vec());
            }

            {
                let mut expected = DirentsSameInodeBuilder::new(fio::INO_UNKNOWN);
                expected.add(fio::DirentType::File, b"more").add(fio::DirentType::File, b"uname");
                assert_read_dirents!(root, 100, expected.into_vec());
            }

            assert_read_dirents!(root, 100, vec![]);
            assert_close!(root);
        }
    });
}

#[test]
fn read_dirents_very_small_buffer() {
    let root = pseudo_directory! {
        "file" => read_only_static(b"Content"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| {
        async move {
            // Entry header is 10 bytes, so this read should not be able to return a single entry.
            assert_read_dirents_err!(root, 8, Status::BUFFER_TOO_SMALL);
            assert_close!(root);
        }
    });
}

#[test]
fn read_dirents_rewind() {
    let root = pseudo_directory! {
        "etc" => pseudo_directory! { },
        "files" => read_only_static(b"Content"),
        "more" => read_only_static(b"Content"),
        "uname" => read_only_static(b"Fuchsia"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| {
        async move {
            {
                let mut expected = DirentsSameInodeBuilder::new(fio::INO_UNKNOWN);
                // Entry header is 10 bytes + length of the name in bytes.
                expected
                    // (10 + 1) = 11
                    .add(fio::DirentType::Directory, b".")
                    // 11 + (10 + 3) = 24
                    .add(fio::DirentType::Directory, b"etc")
                    // 24 + (10 + 5) = 39
                    .add(fio::DirentType::File, b"files");
                assert_read_dirents!(root, 39, expected.into_vec());
            }

            assert_rewind!(root);

            {
                let mut expected = DirentsSameInodeBuilder::new(fio::INO_UNKNOWN);
                // Entry header is 10 bytes + length of the name in bytes.
                expected
                    // (10 + 1) = 11
                    .add(fio::DirentType::Directory, b".")
                    // 11 + (10 + 3) = 24
                    .add(fio::DirentType::Directory, b"etc");
                assert_read_dirents!(root, 24, expected.into_vec());
            }

            {
                let mut expected = DirentsSameInodeBuilder::new(fio::INO_UNKNOWN);
                expected
                    .add(fio::DirentType::File, b"files")
                    .add(fio::DirentType::File, b"more")
                    .add(fio::DirentType::File, b"uname");
                assert_read_dirents!(root, 200, expected.into_vec());
            }

            assert_read_dirents!(root, 100, vec![]);
            assert_close!(root);
        }
    });
}

#[test]
fn add_entry_too_long_error() {
    assert_eq_size!(u64, usize);

    // It is annoying to have to write `as u64` or `as usize` everywhere.  Converting
    // `MAX_FILENAME` to `usize` aligns the types.
    let max_filename = fio::MAX_FILENAME as usize;

    let root = simple();
    let name = {
        let mut name = "This entry name will be longer than the MAX_FILENAME bytes".to_string();

        // Make `name` at least `MAX_FILENAME + 1` bytes long.
        name.reserve(max_filename + 1);
        let filler = " - filler";
        name.push_str(&filler.repeat((max_filename + filler.len()) / filler.len()));

        // And we want exaclty `MAX_FILENAME + 1` bytes.  As all the characters are ASCII, we
        // should be able to just cut at any byte.
        name.truncate(max_filename + 1);
        assert!(name.len() == max_filename + 1);

        name
    };
    let name_len = name.len();

    match root.clone().add_entry(name, read_only_static(b"Should never be used")) {
        Ok(()) => panic!(
            "`add_entry()` succeeded for a name of {} bytes, when MAX_FILENAME is {}",
            name_len, max_filename
        ),
        Err(Status::INVALID_ARGS) => (),
        Err(status) => panic!(
            "`add_entry()` failed for a name of {} bytes, with status {}.  Expected status is \
             INVALID_ARGS.  MAX_FILENAME is {}.",
            name_len, status, max_filename
        ),
    }

    // Make sure that after we have seen an error, the entry is not actually inserted.

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let mut expected = DirentsSameInodeBuilder::new(fio::INO_UNKNOWN);
        expected.add(fio::DirentType::Directory, b".");
        assert_read_dirents!(root, 1000, expected.into_vec());
        assert_close!(root);
    });
}

#[test]
fn node_reference_ignores_read_access() {
    let root = pseudo_directory! {
        "file" => read_only_static(b"Content"),
    };

    run_server_client(
        fio::OpenFlags::NODE_REFERENCE | fio::OpenFlags::RIGHT_READABLE,
        root,
        |root| async move {
            open_as_file_assert_err!(
                &root,
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE,
                "file",
                Status::BAD_HANDLE
            );

            clone_as_directory_assert_err!(
                &root,
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE,
                Status::ACCESS_DENIED
            );

            assert_close!(root);
        },
    );
}

#[test]
fn node_reference_ignores_write_access() {
    let root = pseudo_directory! {
        "file" => read_only_static(b"Content"),
    };

    run_server_client(
        fio::OpenFlags::NODE_REFERENCE | fio::OpenFlags::RIGHT_WRITABLE,
        root,
        |root| async move {
            open_as_file_assert_err!(
                &root,
                fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::DESCRIBE,
                "file",
                Status::BAD_HANDLE
            );

            clone_as_directory_assert_err!(
                &root,
                fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::DESCRIBE,
                Status::ACCESS_DENIED
            );

            assert_close!(root);
        },
    );
}

#[test]
fn node_reference_disallows_read_dirents() {
    let root = pseudo_directory! {
        "etc" => pseudo_directory! {
            "fstab" => read_only_static(b"/dev/fs /"),
            "ssh" => pseudo_directory! {
                "sshd_config" => read_only_static(b"# Empty"),
            },
        },
        "files" => read_only_static(b"Content"),
    };

    run_server_client(fio::OpenFlags::NODE_REFERENCE, root, |root| async move {
        assert_eq!(
            root.read_dirents(100).await.expect("read_dirents failed").0,
            sys::ZX_ERR_BAD_HANDLE
        );
        assert_close!(root);
    });
}

#[test]
fn simple_add_file() {
    let root = simple();

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root.clone(), |proxy| async move {
        {
            let file = read_only_static(b"Content");
            root.add_entry("file", file).unwrap();
        }

        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
        open_as_vmo_file_assert_content!(&proxy, flags, "file", "Content");
        assert_close!(proxy);
    });
}

#[test]
fn add_file_to_empty() {
    let etc;
    let root = pseudo_directory! {
        "etc" => pseudo_directory! {
            etc -> /* empty */
        },
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |proxy| async move {
        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;

        open_as_file_assert_err!(&proxy, flags, "etc/fstab", Status::NOT_FOUND);

        {
            let fstab = read_only_static(b"/dev/fs /");
            etc.add_entry("fstab", fstab).unwrap();
        }

        open_as_vmo_file_assert_content!(&proxy, flags, "etc/fstab", "/dev/fs /");
        assert_close!(proxy);
    });
}

#[test]
fn in_tree_open() {
    let ssh;
    let _root = pseudo_directory! {
        "etc" => pseudo_directory! {
            "ssh" => pseudo_directory! {
                ssh ->
                "sshd_config" => read_only_static(b"# Empty"),
            },
        },
    };

    let exec = TestExecutor::new().expect("TestExecutor creation failed");
    let scope = ExecutionScope::new();

    run_client(exec, || async move {
        let (proxy, server_end) =
            create_proxy::<fio::DirectoryMarker>().expect("Failed to create connection endpoints");

        let flags = fio::OpenFlags::RIGHT_READABLE;
        ssh.open(scope, flags, 0, Path::dot(), server_end.into_channel().into());

        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
        open_as_vmo_file_assert_content!(&proxy, flags, "sshd_config", "# Empty");
        assert_close!(proxy);
    });
}

#[test]
fn in_tree_open_path_one_component() {
    let etc;
    let _root = pseudo_directory! {
        "etc" => pseudo_directory! {
            etc ->
            "ssh" => pseudo_directory! {
                "sshd_config" => read_only_static(b"# Empty"),
            },
        },
    };

    let exec = TestExecutor::new().expect("TestExecutor creation failed");
    let scope = ExecutionScope::new();

    run_client(exec, || async move {
        let (proxy, server_end) =
            create_proxy::<fio::DirectoryMarker>().expect("Failed to create connection endpoints");

        let flags = fio::OpenFlags::RIGHT_READABLE;
        let path = Path::validate_and_split("ssh").unwrap();
        etc.open(scope, flags, 0, path, server_end.into_channel().into());

        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;
        open_as_vmo_file_assert_content!(&proxy, flags, "sshd_config", "# Empty");
        assert_close!(proxy);
    });
}

#[test]
fn in_tree_open_path_two_components() {
    let etc;
    let _root = pseudo_directory! {
        "etc" => pseudo_directory! {
            etc ->
            "ssh" => pseudo_directory! {
                "sshd_config" => read_only_static(b"# Empty"),
            },
        },
    };

    let exec = TestExecutor::new().expect("TestExecutor creation failed");
    let scope = ExecutionScope::new();

    run_client(exec, || async move {
        let (proxy, server_end) =
            create_proxy::<fio::FileMarker>().expect("Failed to create connection endpoints");

        let flags = fio::OpenFlags::RIGHT_READABLE;
        let path = Path::validate_and_split("ssh/sshd_config").unwrap();
        etc.open(scope, flags, 0, path, server_end.into_channel().into());

        assert_read!(&proxy, "# Empty");
        assert_close!(proxy);
    });
}

#[test]
fn in_tree_add_file() {
    let etc;
    let root = pseudo_directory! {
        "etc" => pseudo_directory! {
            etc ->
            "ssh" => pseudo_directory! {
                "sshd_config" => read_only_static(b"# Empty"),
            },
            "passwd" => read_only_static(b"[redacted]"),
        },
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;

        open_as_file_assert_err!(&root, flags, "etc/fstab", Status::NOT_FOUND);
        open_as_vmo_file_assert_content!(&root, flags, "etc/passwd", "[redacted]");

        {
            let fstab = read_only_static(b"/dev/fs /");
            etc.add_entry("fstab", fstab).unwrap();
        }

        open_as_vmo_file_assert_content!(&root, flags, "etc/fstab", "/dev/fs /");
        open_as_vmo_file_assert_content!(&root, flags, "etc/passwd", "[redacted]");

        assert_close!(root);
    });
}

#[test]
fn in_tree_remove_file() {
    let etc;
    let root = pseudo_directory! {
        "etc" => pseudo_directory! {
            etc ->
            "fstab" => read_only_static(b"/dev/fs /"),
            "passwd" => read_only_static(b"[redacted]"),
        },
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;

        open_as_vmo_file_assert_content!(&root, flags, "etc/fstab", "/dev/fs /");
        open_as_vmo_file_assert_content!(&root, flags, "etc/passwd", "[redacted]");

        let o_passwd = etc.remove_entry("passwd", false).unwrap();
        match o_passwd {
            None => panic!("remove_entry() did not find 'passwd'"),
            Some(passwd) => {
                let entry_info = passwd.entry_info();
                assert_eq!(entry_info, EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::File));
            }
        }

        open_as_vmo_file_assert_content!(&root, flags, "etc/fstab", "/dev/fs /");
        open_as_file_assert_err!(&root, flags, "etc/passwd", Status::NOT_FOUND);

        assert_close!(root);
    });
}

#[test]
fn in_tree_move_file() {
    let etc;
    let root = pseudo_directory! {
        "etc" => pseudo_directory! {
            etc ->
            "fstab" => read_only_static(b"/dev/fs /"),
        },
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;

        open_as_vmo_file_assert_content!(&root, flags, "etc/fstab", "/dev/fs /");
        open_as_file_assert_err!(&root, flags, "etc/passwd", Status::NOT_FOUND);

        let fstab = etc
            .clone()
            .remove_entry("fstab", false)
            .unwrap()
            .expect("remove_entry() did not find 'fstab'");

        etc.add_entry("passwd", fstab).unwrap();

        open_as_file_assert_err!(&root, flags, "etc/fstab", Status::NOT_FOUND);
        open_as_vmo_file_assert_content!(&root, flags, "etc/passwd", "/dev/fs /");

        assert_close!(root);
    });
}

#[test]
fn watch_empty() {
    run_server_client(fio::OpenFlags::RIGHT_READABLE, simple(), |root| async move {
        let mask = fio::WatchMask::EXISTING
            | fio::WatchMask::IDLE
            | fio::WatchMask::ADDED
            | fio::WatchMask::REMOVED;
        let watcher_client = assert_watch!(root, mask);

        assert_watcher_one_message_watched_events!(watcher_client, { EXISTING, "." });
        assert_watcher_one_message_watched_events!(watcher_client, { IDLE, vec![] });

        drop(watcher_client);
        assert_close!(root);
    });
}

#[test]
fn watch_non_empty() {
    let root = pseudo_directory! {
        "etc" => pseudo_directory! {
            "fstab" => read_only_static(b"/dev/fs /"),
            "ssh" => pseudo_directory! {
                "sshd_config" => read_only_static(b"# Empty"),
            },
        },
        "files" => read_only_static(b"Content"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let mask = fio::WatchMask::EXISTING
            | fio::WatchMask::IDLE
            | fio::WatchMask::ADDED
            | fio::WatchMask::REMOVED;
        let watcher_client = assert_watch!(root, mask);

        assert_watcher_one_message_watched_events!(
            watcher_client,
            { EXISTING, "." },
            { EXISTING, "etc" },
            { EXISTING, "files" },
        );
        assert_watcher_one_message_watched_events!(watcher_client, { IDLE, vec![] });

        drop(watcher_client);
        assert_close!(root);
    });
}

#[test]
fn watch_two_watchers() {
    let root = pseudo_directory! {
        "etc" => pseudo_directory! {
            "fstab" => read_only_static(b"/dev/fs /"),
            "ssh" => pseudo_directory! {
                "sshd_config" => read_only_static(b"# Empty"),
            },
        },
        "files" => read_only_static(b"Content"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let mask = fio::WatchMask::EXISTING
            | fio::WatchMask::IDLE
            | fio::WatchMask::ADDED
            | fio::WatchMask::REMOVED;
        let watcher1_client = assert_watch!(root, mask);

        assert_watcher_one_message_watched_events!(
            watcher1_client,
            { EXISTING, "." },
            { EXISTING, "etc" },
            { EXISTING, "files" },
        );
        assert_watcher_one_message_watched_events!(watcher1_client, { IDLE, vec![] });

        let watcher2_client = assert_watch!(root, mask);

        assert_watcher_one_message_watched_events!(
            watcher2_client,
            { EXISTING, "." },
            { EXISTING, "etc" },
            { EXISTING, "files" },
        );
        assert_watcher_one_message_watched_events!(watcher2_client, { IDLE, vec![] });

        drop(watcher1_client);
        drop(watcher2_client);
        assert_close!(root);
    });
}

#[test]
fn watch_addition() {
    let etc;
    let root = pseudo_directory! {
        "etc" => pseudo_directory! {
            etc ->
            "ssh" => pseudo_directory! {
                "sshd_config" => read_only_static(b"# Empty"),
            },
            "passwd" => read_only_static(b"[redacted]"),
        },
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;

        open_as_file_assert_err!(&root, flags, "etc/fstab", Status::NOT_FOUND);
        open_as_vmo_file_assert_content!(&root, flags, "etc/passwd", "[redacted]");

        let etc_proxy = open_get_directory_proxy_assert_ok!(&root, flags, "etc");

        let watch_mask = fio::WatchMask::EXISTING
            | fio::WatchMask::IDLE
            | fio::WatchMask::ADDED
            | fio::WatchMask::REMOVED;
        let watcher = assert_watch!(etc_proxy, watch_mask);

        assert_watcher_one_message_watched_events!(
            watcher,
            { EXISTING, "." },
            { EXISTING, "passwd" },
            { EXISTING, "ssh" },
        );
        assert_watcher_one_message_watched_events!(watcher, { IDLE, vec![] });

        {
            let fstab = read_only_static(b"/dev/fs /");
            etc.add_entry("fstab", fstab).unwrap();
        }

        assert_watcher_one_message_watched_events!(watcher, { ADDED, "fstab" });

        open_as_vmo_file_assert_content!(&root, flags, "etc/fstab", "/dev/fs /");
        open_as_vmo_file_assert_content!(&root, flags, "etc/passwd", "[redacted]");

        assert_close!(etc_proxy);
        assert_close!(root);
    });
}

#[test]
fn watch_removal() {
    let etc;
    let root = pseudo_directory! {
        "etc" => pseudo_directory! {
            etc ->
            "fstab" => read_only_static(b"/dev/fs /"),
            "passwd" => read_only_static(b"[redacted]"),
        },
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;

        open_as_vmo_file_assert_content!(&root, flags, "etc/fstab", "/dev/fs /");
        open_as_vmo_file_assert_content!(&root, flags, "etc/passwd", "[redacted]");

        let etc_proxy = open_get_directory_proxy_assert_ok!(&root, flags, "etc");

        let watch_mask = fio::WatchMask::EXISTING
            | fio::WatchMask::IDLE
            | fio::WatchMask::ADDED
            | fio::WatchMask::REMOVED;
        let watcher = assert_watch!(etc_proxy, watch_mask);

        assert_watcher_one_message_watched_events!(
            watcher,
            { EXISTING, "." },
            { EXISTING, "fstab" },
            { EXISTING, "passwd" },
        );
        assert_watcher_one_message_watched_events!(watcher, { IDLE, vec![] });

        let o_passwd = etc.remove_entry("passwd", false).unwrap();
        match o_passwd {
            None => panic!("remove_entry() did not find 'passwd'"),
            Some(passwd) => {
                let entry_info = passwd.entry_info();
                assert_eq!(entry_info, EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::File));
            }
        }

        assert_watcher_one_message_watched_events!(watcher, { REMOVED, "passwd" });

        open_as_vmo_file_assert_content!(&root, flags, "etc/fstab", "/dev/fs /");
        open_as_file_assert_err!(&root, flags, "etc/passwd", Status::NOT_FOUND);

        assert_close!(etc_proxy);
        assert_close!(root);
    });
}

#[test]
fn watch_with_mask() {
    let root = pseudo_directory! {
        "etc" => pseudo_directory! {
            "fstab" => read_only_static(b"/dev/fs /"),
            "ssh" => pseudo_directory! {
                "sshd_config" => read_only_static(b"# Empty"),
            },
        },
        "files" => read_only_static(b"Content"),
    };

    run_server_client(fio::OpenFlags::RIGHT_READABLE, root, |root| async move {
        let mask = fio::WatchMask::IDLE | fio::WatchMask::ADDED | fio::WatchMask::REMOVED;
        let watcher_client = assert_watch!(root, mask);

        assert_watcher_one_message_watched_events!(watcher_client, { IDLE, vec![] });

        drop(watcher_client);
        assert_close!(root);
    });
}

#[test]
fn watch_addition_with_two_scopes() {
    let etc;
    let root = pseudo_directory! {
        "etc" => pseudo_directory! {
            etc ->
            "passwd" => read_only_static(b"[redacted]"),
        },
    };

    let exec = TestExecutor::new().expect("TestExecutor creation failed");
    let scope1 = ExecutionScope::new();
    let scope2 = ExecutionScope::new();

    run_client(exec, || {
        async move {
            async fn open_with_scope(
                server: Arc<dyn DirectoryEntry>,
                scope: ExecutionScope,
            ) -> fio::DirectoryProxy {
                let (proxy, server_end) = create_proxy::<fio::DirectoryMarker>()
                    .expect("Failed to create connection endpoints");

                let flags = fio::OpenFlags::RIGHT_READABLE;
                server.open(scope, flags, 0, Path::dot(), server_end.into_channel().into());
                proxy
            }

            let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE;

            let root_proxy = open_with_scope(root.clone(), scope1.clone()).await;

            open_as_file_assert_err!(&root_proxy, flags, "etc/fstab", Status::NOT_FOUND);
            open_as_vmo_file_assert_content!(&root_proxy, flags, "etc/passwd", "[redacted]");

            let etc1_proxy = open_with_scope(etc.clone(), scope1.clone()).await;
            open_as_file_assert_err!(&etc1_proxy, flags, "fstab", Status::NOT_FOUND);
            open_as_vmo_file_assert_content!(&etc1_proxy, flags, "passwd", "[redacted]");

            let etc2_proxy = open_with_scope(etc.clone(), scope2.clone()).await;
            open_as_file_assert_err!(&etc2_proxy, flags, "fstab", Status::NOT_FOUND);
            open_as_vmo_file_assert_content!(&etc2_proxy, flags, "passwd", "[redacted]");

            let mask = fio::WatchMask::ADDED | fio::WatchMask::REMOVED;
            let watcher1_client = assert_watch!(etc1_proxy, mask);
            let watcher2_client = assert_watch!(etc2_proxy, mask);

            {
                let fstab = read_only_static(b"/dev/fs /");
                etc.clone().add_entry("fstab", fstab).unwrap();
            }

            assert_watcher_one_message_watched_events!(watcher1_client, { ADDED, "fstab" });
            assert_watcher_one_message_watched_events!(watcher2_client, { ADDED, "fstab" });

            open_as_vmo_file_assert_content!(&root_proxy, flags, "etc/fstab", "/dev/fs /");
            open_as_vmo_file_assert_content!(&root_proxy, flags, "etc/passwd", "[redacted]");

            scope2.shutdown();

            // Wait for the shutdown to go through.
            assert_channel_closed!(watcher2_client);
            // Our etc2_proxy is also using the second scope, so it should go down as well.
            assert_channel_closed!(etc2_proxy.into_channel().unwrap());

            {
                let shells = read_only_static(b"/bin/bash");
                etc.add_entry("shells", shells).unwrap();
            }

            assert_watcher_one_message_watched_events!(watcher1_client, { ADDED, "shells" });

            open_as_vmo_file_assert_content!(&root_proxy, flags, "etc/fstab", "/dev/fs /");
            open_as_vmo_file_assert_content!(&root_proxy, flags, "etc/passwd", "[redacted]");
            open_as_vmo_file_assert_content!(&root_proxy, flags, "etc/shells", "/bin/bash");

            drop(watcher1_client);
            drop(watcher2_client);

            assert_close!(etc1_proxy);
            assert_close!(root_proxy);
        }
    });
}
