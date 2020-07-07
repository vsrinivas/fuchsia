// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use fidl::endpoints::RequestStream;
use fidl_fidl_test_after as after_fidl;
use fidl_fidl_test_before as before_fidl;
use fidl_fidl_test_during as during_fidl;
use fuchsia_async as fasync;
use futures::prelude::*;

#[macro_use]
mod before;
#[macro_use]
mod during;
#[macro_use]
mod after;

// This file tests source-assisted transitions in Rust. For details on the
// steps, see src/tests/fidl/source_compatibility/README.md#source-assisted.

// Step 1: test that the original code compiles against the original library.
mod before_before {
    use super::*;
    before_impl!(before_fidl);
}

// Step 2: test that the transitional code compiles against the original library.
mod during_before {
    use super::*;
    during_impl!(before_fidl);
}

// Step 3: test that the transitional code compiles against the transitional library.
mod during_during {
    use super::*;
    during_impl!(during_fidl);
}

// Step 4: test that the transitional code compiles against the final library.
mod during_after {
    use super::*;
    during_impl!(after_fidl);
}

// Step 5: test that the final code compiles against the final library.
mod after_after {
    use super::*;
    after_impl!(after_fidl);
}
