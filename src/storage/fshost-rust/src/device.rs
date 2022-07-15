// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::PathBuf;

pub trait Device {
    fn path(&self) -> PathBuf;
}

/// A block device.
#[derive(Clone, Debug)]
pub struct BlockDevice {
    pub path: PathBuf,
}

impl Device for BlockDevice {
    fn path(&self) -> PathBuf {
        self.path.clone()
    }
}
