// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::api::Hash as HashApi;
use std::fmt;

static EMPTY: Vec<u8> = vec![];

/// TODO(fxbug.dev/111241): Implement hash for blob API (and others).
#[derive(Debug, Default)]
pub(crate) struct Hash;

impl fmt::Display for Hash {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "<<unknown scrutiny_x::hash::Hash>>")
    }
}

impl HashApi for Hash {
    fn as_bytes(&self) -> &[u8] {
        EMPTY.as_slice()
    }
}
