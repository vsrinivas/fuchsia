// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod args;

fn main() {
    let _: crate::args::Args = argh::from_env();
    println!("All new projects need to start somewhere.");
}
