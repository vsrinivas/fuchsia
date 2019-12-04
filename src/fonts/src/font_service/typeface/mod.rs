// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

mod collection;
mod matcher;
mod typeface;

pub use {
    collection::Collection,
    typeface::{Typeface, TypefaceInfoAndCharSet},
};
