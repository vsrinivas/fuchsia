// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use super::codegen_test;

mod c {
    use super::*;

    codegen_test!(alignment, CBackend, ["banjo/alignment.test.banjo"], "c/alignment.h");
}
