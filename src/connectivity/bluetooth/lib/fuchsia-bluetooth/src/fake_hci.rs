// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {failure::Error, std::fs::File};

use crate::hci;

/// Represents a fake bt-hci device. Closes the underlying device when it goes out of scope.
pub struct FakeHciDevice(File);

impl FakeHciDevice {
    /// Publishes a new fake bt-hci device and constructs a FakeHciDevice with it.
    pub fn new() -> Result<FakeHciDevice, Error> {
        let (dev, _) = hci::create_and_bind_device()?;
        Ok(FakeHciDevice(dev))
    }

    /// Returns a reference to the underlying file.
    pub fn file(&self) -> &File {
        &self.0
    }
}

impl Drop for FakeHciDevice {
    fn drop(&mut self) {
        hci::destroy_device(&self.0).unwrap();
    }
}
