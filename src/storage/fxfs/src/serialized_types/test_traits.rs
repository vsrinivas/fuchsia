// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file saves some repetition in tests where integers are used in place of keys and values
// by just implementing the traits for some basic types here.
use crate::serialized_types::{versioned_type, Version, VersionLatest, VersionNumber};

// The traits for u64 are implemented by hand to illustrate what the proc_macro is doing.
impl Version for u64 {
    fn version() -> VersionNumber {
        VersionNumber { major: 1, minor: 0 }
    }
}
impl VersionLatest for u64 {
    fn deserialize_from_version<R: ?Sized>(
        reader: &mut R,
        version: VersionNumber,
    ) -> anyhow::Result<Self>
    where
        R: std::io::Read,
        Self: Sized,
    {
        match version.major {
            1 => Ok(u64::deserialize_from(reader)?.into()),
            _ => anyhow::bail!(format!("Invalid version {} for u64.", version)),
        }
    }
}

versioned_type! { 1 => i64 }
versioned_type! { 1 => u32 }
versioned_type! { 1 => i32 }
versioned_type! { 1 => u8 }
versioned_type! { 1 => i8 }
