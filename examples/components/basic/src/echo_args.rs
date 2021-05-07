// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use tracing::info;

#[fuchsia::component]
fn main() {
    let args: Vec<String> = std::env::args().collect();
    info!("{}", args.join(" "));
}
