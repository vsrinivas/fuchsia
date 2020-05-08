// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

fn main() {
    fuchsia_syslog::init().expect("initializing logging");
    log::warn!("my warn message.");
    log::error!("my error message.");
}
