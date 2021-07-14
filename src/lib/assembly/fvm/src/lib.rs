// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Library for constructing the FVM.

mod fvm;
mod nand;

pub use fvm::{Filesystem, FilesystemAttributes, FvmBuilder, FvmType};
pub use nand::NandFvmBuilder;
