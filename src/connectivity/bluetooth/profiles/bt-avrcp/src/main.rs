// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![feature(async_await, await_macro)]
#![recursion_limit = "256"]
#![allow(dead_code)] // TODO(BT-2218): This is temporary until all of AVRCP lands.

use {failure::Error, fuchsia_async as fasync, fuchsia_syslog};

mod packets;
mod types;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["avrcp", "avctp"]).expect("Can't init logger");

    Ok(())
}
