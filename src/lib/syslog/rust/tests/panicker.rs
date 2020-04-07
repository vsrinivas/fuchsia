// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_syslog as syslog;

fn main() {
    syslog::init_with_tags(&["panicker"]).expect("should not fail");
    panic!("oh no, I panicked");
}
