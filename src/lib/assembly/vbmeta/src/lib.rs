// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Library for constructing and serializing the VBMeta struct for verified boot.

mod descriptor;

pub use descriptor::{HashDescriptor, Salt, SaltError};
