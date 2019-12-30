// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Use no_mangle and extern "C" to export the function as a C ABI symbol.
#[no_mangle]
pub extern "C" fn crust_get_int() -> i32 {
    42
}
