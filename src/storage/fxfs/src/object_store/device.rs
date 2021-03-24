// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;

// TODO(jfsulliv): Make async
/// Interface to devices.
pub trait Device: Send + Sync {
    fn block_size(&self) -> u64;

    fn read(&self, offset: u64, buf: &mut [u8]) -> Result<(), Error>;

    fn write(&self, offset: u64, buf: &[u8]) -> Result<(), Error>;
}
