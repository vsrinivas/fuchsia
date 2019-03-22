// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use log::info;

fn main() {
    fuchsia_syslog::init().expect("Can't init logger");
    info!("Starting omaha client...");
}
