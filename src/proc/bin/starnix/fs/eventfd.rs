// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::*;
use crate::task::*;
use crate::types::*;

pub fn new_eventfd(kernel: &Kernel, _value: u32, flags: OpenFlags) -> FileHandle {
    let fs = anon_fs(kernel);
    Anon::new_file(fs, Box::new(NullFile), flags)
}
