// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

fn main() {
    let num = 9;
    #[allow(clippy::while_immutable_condition)] // TODO(fxbug.dev/95052)
    while num >= 10 {}
}
