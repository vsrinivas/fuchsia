// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

fn main() {
    println!("{:#?}", receiver_config::Config::take_from_startup_handle());
}
