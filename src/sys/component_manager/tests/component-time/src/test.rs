// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::SystemTime;

#[test]
fn component_can_read_time() {
    let now = SystemTime::now()
        .duration_since(SystemTime::UNIX_EPOCH)
        .expect("SystemTime before UNIX EPOCH");

    // Fuchsia always has a backstop time greater than the UNIX EPOCH.
    assert!(now.as_millis() > 0);
}
