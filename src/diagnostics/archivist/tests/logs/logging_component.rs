// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

fn main() {
    fuchsia_syslog::init().expect("initializing logging");
    // TODO(42169): Change these once fxr/375515 lands
    fuchsia_syslog::set_verbosity(1);
    log::debug!("my debug message.");
    log::info!("my info message.");
    log::warn!("my warn message.");
}
