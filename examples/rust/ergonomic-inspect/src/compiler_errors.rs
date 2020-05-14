// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This file contains code that does not compile, and is used for negative
//! examples.

// [START derive_inspect_unwrapped]
#[derive(Inspect)]
struct Yakling {
    name: String, // Forgot to wrap, should be `name: IValue<String>`
}

// error[E0599]: no method named `iattach` found for struct
// `std::string::String` in the current scope
// [END derive_inspect_unwrapped]
