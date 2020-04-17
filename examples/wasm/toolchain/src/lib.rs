// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[no_mangle]
pub extern "C" fn exported_wasm_fn() -> u32 {
    66 + 87 + 66
}
