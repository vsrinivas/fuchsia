// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Library for reading and writing a config describing which images to
//! generate and how.

mod images_config;

pub use images_config::Fvm;
pub use images_config::{BlobFS, BlobFSLayout, EmptyMinFS, FvmFilesystem, MinFS, Reserved};
pub use images_config::{FvmOutput, NandFvm, SparseFvm, StandardFvm};
pub use images_config::{Image, ImagesConfig};
pub use images_config::{PostProcessingScript, Zbi, ZbiCompression};
pub use images_config::{VBMeta, VBMetaDescriptor};
