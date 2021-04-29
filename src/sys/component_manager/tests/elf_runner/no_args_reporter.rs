// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

fn main() {
    let args: Vec<String> = std::env::args().collect();
    assert_eq!(args, vec!["/pkg/bin/no_args_reporter"]);
}
