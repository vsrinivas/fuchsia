// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

fn main() {
    fuchsia_syslog::init_with_tags(&["logging component"]).expect("initializing logging");
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::DEBUG);
    log::debug!("my debug message.");
    log::info!("my info message.");
    log::warn!("my warn message.");
}
