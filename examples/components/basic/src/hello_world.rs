// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_syslog::{self, fx_log_info};

fn main() {
    fuchsia_syslog::init_with_tags(&["hello_world"]).expect("failed to initialize logger");
    fx_log_info!("Hippo: Hello World!");
}

#[cfg(test)]
mod tests {
    #[test]
    fn assert_0_is_0() {
        assert_eq!(0, 0);
    }
}
