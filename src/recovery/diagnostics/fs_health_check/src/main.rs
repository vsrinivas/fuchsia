// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;

#[fasync::run_singlethreaded]
async fn main() {
    // TODO(b/205008040): Implement health check.
    eprintln!("fs_health_check stub");
}
