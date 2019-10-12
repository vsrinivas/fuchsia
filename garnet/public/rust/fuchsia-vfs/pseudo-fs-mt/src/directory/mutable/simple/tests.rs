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
    assert_close, assert_event, assert_read, assert_unlink, assert_unlink_err,
    open_as_file_assert_err, open_get_file_proxy_assert_ok, open_get_proxy_assert,
};

use crate::{directory::test_utils::run_server_client, file::pcb::asynchronous::read_only_static};

use {
    fidl_fuchsia_io::{OPEN_FLAG_DESCRIBE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    proc_macro_hack::proc_macro_hack,
};

// Create level import of this macro does not affect nested modules.  And as attributes can
// only be applied to the whole "use" directive, this need to be present here and need to be
// separate form the above.  "use crate::pseudo_directory" generates a warning referring to
// "issue #52234 <https://github.com/rust-lang/rust/issues/52234>".
#[proc_macro_hack(support_nested)]
use fuchsia_vfs_pseudo_fs_mt_macros::mut_pseudo_directory;

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
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

            open_as_file_assert_content!(&proxy, flags, "fstab", "/dev/fs /");
            open_as_file_assert_content!(&proxy, flags, "passwd", "[redacted]");

            assert_unlink!(&proxy, "passwd");

            open_as_file_assert_content!(&proxy, flags, "fstab", "/dev/fs /");
            open_as_file_assert_err!(&proxy, flags, "passwd", Status::NOT_FOUND);

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
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

            open_as_file_assert_content!(&proxy, flags, "fstab", "/dev/fs /");

            assert_unlink_err!(&proxy, "fstab.2", Status::NOT_FOUND);

            open_as_file_assert_content!(&proxy, flags, "fstab", "/dev/fs /");

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
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

            open_as_file_assert_content!(&proxy, flags, "etc/fstab", "/dev/fs /");

            assert_unlink_err!(&proxy, "etc/fstab", Status::BAD_PATH);

            open_as_file_assert_content!(&proxy, flags, "etc/fstab", "/dev/fs /");

            assert_close!(proxy);
        }
    });
}
