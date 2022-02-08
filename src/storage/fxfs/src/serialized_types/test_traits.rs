// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file saves some repetition in tests where integers are used in place of keys and values
// by just implementing the traits for some basic types here.
use crate::serialized_types::{
    versioned_type, Version, Versioned, VersionedLatest, LATEST_VERSION,
};

// The traits for u64 are implemented by hand to illustrate what the proc_macro is doing.
impl Versioned for u64 {}
impl VersionedLatest for u64 {
    fn deserialize_from_version<R: ?Sized>(reader: &mut R, version: Version) -> anyhow::Result<Self>
    where
        R: std::io::Read,
        Self: Sized,
    {
        match version.major {
            1.. => Ok(u64::deserialize_from(reader, version)?.into()),
            _ => anyhow::bail!(format!("Invalid version {} for u64.", version)),
        }
    }
}

impl Versioned for i64 {}
versioned_type! { 1.. => i64 }
impl Versioned for u32 {}
versioned_type! { 1.. => u32 }
impl Versioned for i32 {}
versioned_type! { 1.. => i32 }
impl Versioned for u8 {}
versioned_type! { 1.. => u8 }
impl Versioned for i8 {}
versioned_type! { 1.. => i8 }
