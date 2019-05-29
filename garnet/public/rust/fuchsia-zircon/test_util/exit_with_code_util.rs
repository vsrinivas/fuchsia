// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

fn main() {
    let arg = std::env::args().next().expect("Expected one argument");
    let code = arg.parse::<i64>().expect("Failed to parse argument as i64");
    zx::Process::exit(code);
}
