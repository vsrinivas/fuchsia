// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io::Result;

pub trait PackageGetter: Send + Sync {
    fn read_raw(&self, path: &str) -> Result<Vec<u8>>;
}
