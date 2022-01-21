// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

/// This trait is assigned to all versions of a versioned type.
/// It allows type versions to serialize/deserialize themselves differently on a per-version basis.
pub trait Version: Serialize + for<'de> Deserialize<'de> {
    fn version() -> u32;
    fn deserialize_from<R: ?Sized>(reader: &mut R) -> anyhow::Result<Self>
    where
        R: std::io::Read,
        for<'de> Self: serde::Deserialize<'de>,
    {
        match bincode::deserialize_from(reader) {
            // Strip bincode wrapping. anyhow can take std::io::Error.
            Err(e) => Err(if let bincode::ErrorKind::Io(e) = *e { e.into() } else { e.into() }),
            Ok(t) => Ok(t),
        }
    }
    fn serialize_into<W>(&self, writer: &mut W) -> anyhow::Result<()>
    where
        W: std::io::Write,
        Self: serde::Serialize,
    {
        match bincode::serialize_into(writer, self) {
            // Strip bincode wrapping. anyhow can take std::io::Error.
            Err(e) => Err(if let bincode::ErrorKind::Io(e) = *e { e.into() } else { e.into() }),
            Ok(t) => Ok(t),
        }
    }
}

/// This trait is only assigned to the latest version of a type and allows the type to deserialize
/// any older versions and upgrade them to the latest format.
pub trait VersionLatest {
    /// Deserializes from a given version format and upgrades to the latest version.
    fn deserialize_from_version<R>(reader: &mut R, version: u32) -> anyhow::Result<Self>
    where
        R: std::io::Read,
        Self: Sized;
}
