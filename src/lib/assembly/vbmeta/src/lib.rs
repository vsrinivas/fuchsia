// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Library for constructing and serializing the VBMeta struct for verified boot.

mod descriptor;
mod header;
mod key;
mod test;
mod vbmeta;

pub use descriptor::{builder::RawHashDescriptorBuilder, HashDescriptor, Salt, SaltError};
pub use header::Header;
pub use key::{Key, KeyError, SignFailure, Signature, SIGNATURE_SIZE};
pub use vbmeta::VBMeta;
