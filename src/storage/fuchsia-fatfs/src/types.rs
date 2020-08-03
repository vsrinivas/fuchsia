// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! These type declarations simply exist to reduce the amount of boilerplate in the other parts of
//! this crate.

use fatfs::{DefaultTimeProvider, LossyOemCpConverter, ReadWriteSeek};

pub type FileSystem =
    fatfs::FileSystem<Box<dyn ReadWriteSeek + Send>, DefaultTimeProvider, LossyOemCpConverter>;
pub type Dir<'a> =
    fatfs::Dir<'a, Box<dyn ReadWriteSeek + Send>, DefaultTimeProvider, LossyOemCpConverter>;
pub type DirEntry<'a> =
    fatfs::DirEntry<'a, Box<dyn ReadWriteSeek + Send>, DefaultTimeProvider, LossyOemCpConverter>;
pub type File<'a> =
    fatfs::File<'a, Box<dyn ReadWriteSeek + Send>, DefaultTimeProvider, LossyOemCpConverter>;
