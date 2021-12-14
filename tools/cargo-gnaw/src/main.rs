// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, gnaw_lib};

fn main() -> Result<(), Error> {
    let args: Vec<String> = std::env::args().collect();
    gnaw_lib::run(&args[..])
}
