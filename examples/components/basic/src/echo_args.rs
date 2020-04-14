// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_syslog::{self, fx_log_info};

fn main() {
    fuchsia_syslog::init_with_tags(&["hello_world"]).expect("failed to initialize logger");
    let args: Vec<String> = std::env::args().collect();
    fx_log_info!("{}", args.join(" "));
}
