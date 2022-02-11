// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Utility methods and traits used throughout assembly.

mod insert_unique;
mod path_to_string;

pub use insert_unique::{DuplicateKeyError, InsertAllUniqueExt, InsertUniqueExt, MapEntry};
pub use path_to_string::PathToStringExt;
