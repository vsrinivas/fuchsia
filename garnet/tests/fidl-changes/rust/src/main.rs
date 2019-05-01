// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
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

// Test that the original code compiles against the original library.
before_impl!(BeforeBefore, before_fidl);
// Test that the pre-flighted code compiles against the original library.
during_impl!(DuringBefore, before_fidl);
// Test that the pre-flighted code compiles against the transitional library.
during_impl!(DuringDuring, during_fidl);
// Test that the pre-flighted code compiles against the final library.
during_impl!(DuringAfter, after_fidl);
// Test that the final code compiles against the final library.
after_impl!(AfterAfter, after_fidl);

fn main() {}
