// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::device::Device, anyhow::Result};

/// The struct which owns all the matchers for a particular partition layout, and can decide where
/// in that layout a block device fits.
pub struct Matcher;

impl Matcher {
    /// Create a new set of matchers. This essentially describes the expected partition layout for
    /// a device.
    pub fn new() -> Matcher {
        Matcher
    }

    /// Using the set of matchers we created, figure out if this block device matches any of our
    /// expected partitions. If it does, return the information needed to launch the filesystem,
    /// such as the component url or the shared library to pass to the driver binding.
    pub async fn match_device(&self, _device: Box<dyn Device>) -> Result<()> {
        Ok(())
    }
}
