// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! These type declarations simply exist to reduce the amount of boilerplate in the other parts of
//! this crate.

use fatfs::{DefaultTimeProvider, LossyOemCpConverter, ReadWriteSeek};

pub trait Disk: ReadWriteSeek + Send {
    /// Returns true if the underlying block device for this disk is still present.
    fn is_present(&self) -> bool;
}

// Default implementation, used for tests.
impl Disk for std::io::Cursor<Vec<u8>> {
    fn is_present(&self) -> bool {
        true
    }
}

impl Disk for remote_block_device::Cache {
    fn is_present(&self) -> bool {
        self.device().is_connected()
    }
}

pub type FileSystem = fatfs::FileSystem<Box<dyn Disk>, DefaultTimeProvider, LossyOemCpConverter>;
pub type Dir<'a> = fatfs::Dir<'a, Box<dyn Disk>, DefaultTimeProvider, LossyOemCpConverter>;
pub type DirEntry<'a> =
    fatfs::DirEntry<'a, Box<dyn Disk>, DefaultTimeProvider, LossyOemCpConverter>;
pub type File<'a> = fatfs::File<'a, Box<dyn Disk>, DefaultTimeProvider, LossyOemCpConverter>;
