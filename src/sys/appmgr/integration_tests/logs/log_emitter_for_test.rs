// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

fn main() {
    fuchsia_syslog::init().expect("initializing logging");
    log::info!("hello, diagnostics!");
}
