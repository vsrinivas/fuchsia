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

use crate::assert_close;

use crate::directory::test_utils::run_server_client;

use fidl_fuchsia_io::OPEN_RIGHT_READABLE;

#[test]
fn empty_directory() {
    run_server_client(OPEN_RIGHT_READABLE, simple(), |proxy| {
        async move {
            assert_close!(proxy);
        }
    });
}
