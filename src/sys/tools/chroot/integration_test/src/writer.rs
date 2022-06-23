// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub fn main() {
    // This binary is launched with `/ns` as the root.
    // Because `/ns` is a tmp storage capability, the root
    // directory should be completely writable.
    std::fs::write("/foo.txt", "Hippos rule!").unwrap();
}
