// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use byteorder::{LittleEndian, ReadBytesExt, WriteBytesExt};
use serde::{Deserialize, Serialize};

/// [VersionNumber] are themselves serializable both alone and as part
/// of other [Version] structures.
/// (For obvious recursive reasons, this structure can never be [Version] itself.)
#[derive(Debug, Default, Copy, Clone, Eq, PartialEq, PartialOrd, Serialize, Deserialize)]
pub struct VersionNumber {
    /// Major version indicates structural layout/encoding changes.
    pub major: u16,
    // TODO(ripper): Before we use minor versions we must resolve how to represent
    // multiple minor versions or ensure that after a major bump we reset the minor to
    // zero, etc. For now, we include it for encoding stability but it is currently unused.
    pub minor: u16,
}
impl std::fmt::Display for VersionNumber {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}.{}", self.major, self.minor)
    }
}
impl VersionNumber {
    pub fn deserialize_from<R: ?Sized>(reader: &mut R) -> anyhow::Result<Self>
    where
        R: std::io::Read,
    {
        Ok(VersionNumber {
            major: reader.read_u16::<LittleEndian>()?,
            minor: reader.read_u16::<LittleEndian>()?,
        })
    }
    pub fn serialize_into<W>(&self, writer: &mut W) -> anyhow::Result<()>
    where
        W: std::io::Write,
    {
        writer.write_u16::<LittleEndian>(self.major)?;
        writer.write_u16::<LittleEndian>(self.minor)?;
        Ok(())
    }
}

/// This trait is assigned to all versions of a versioned type.
/// It allows type versions to serialize/deserialize themselves differently on a per-version basis.
pub trait Version: Serialize + for<'de> Deserialize<'de> {
    fn version() -> VersionNumber;
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
    /// Like `serialize_into` but serialized VersionNumber first, then self.
    fn serialize_with_version<W>(&self, writer: &mut W) -> anyhow::Result<()>
    where
        W: std::io::Write,
        Self: Sized,
    {
        Self::version().serialize_into(writer)?;
        self.serialize_into(writer)
    }
}

/// This trait is only assigned to the latest version of a type and allows the type to deserialize
/// any older versions and upgrade them to the latest format.
pub trait VersionLatest {
    /// Deserializes from a given version format and upgrades to the latest version.
    fn deserialize_from_version<R>(reader: &mut R, version: VersionNumber) -> anyhow::Result<Self>
    where
        R: std::io::Read,
        Self: Sized;

    /// Like `deserialize_from_version` but reads VersionNumber from reader first, then uses it to
    /// deserialize self.
    fn deserialize_with_version<R>(reader: &mut R) -> anyhow::Result<(Self, VersionNumber)>
    where
        R: std::io::Read,
        Self: Sized,
    {
        let version = VersionNumber::deserialize_from(reader)?;
        Ok((Self::deserialize_from_version(reader, version)?, version))
    }
}
