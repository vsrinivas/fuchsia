// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{collections::HashSet, io::Result};

/// Interface for fetching raw package bytes by file path.
pub trait PackageGetter: Send + Sync {
    /// Read the raw bytes stored in filesystem location `path`.
    fn read_raw(&mut self, path: &str) -> Result<Vec<u8>>;

    /// Get the accumulated set of filesystem locations that have been read by
    /// this getter.
    fn get_deps(&self) -> HashSet<String>;
}
