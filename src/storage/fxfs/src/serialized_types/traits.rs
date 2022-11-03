// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::serialized_types::{types::LATEST_VERSION, DEFAULT_MAX_SERIALIZED_RECORD_SIZE},
    byteorder::{LittleEndian, ReadBytesExt, WriteBytesExt},
    serde::{Deserialize, Serialize},
    type_hash::TypeHash,
};

/// [Version] are themselves serializable both alone and as part
/// of other [Versioned] structures.
/// (For obvious recursive reasons, this structure can never be [Versioned] itself.)
#[derive(
    Debug, Default, Copy, Clone, Eq, PartialEq, PartialOrd, Serialize, Deserialize, TypeHash,
)]
pub struct Version {
    /// Major version indicates structural layout/encoding changes.
    /// Note that this is encoded as a u24.
    pub major: u32,
    /// Minor version indicates forwards compatible changes.
    /// e.g. The addition of a layer-file index, bloom filters, file attributes or posix
    /// features where reversion to a previous minor will simply lead to loss of these features.
    pub minor: u8,
}
impl std::fmt::Display for Version {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}.{}", self.major, self.minor)
    }
}
impl Version {
    pub fn deserialize_from<R: ?Sized>(reader: &mut R) -> anyhow::Result<Self>
    where
        R: std::io::Read,
    {
        Ok(Version { major: reader.read_u24::<LittleEndian>()?, minor: reader.read_u8()? })
    }
    pub fn serialize_into<W>(&self, writer: &mut W) -> anyhow::Result<()>
    where
        W: std::io::Write,
    {
        writer.write_u24::<LittleEndian>(self.major)?;
        writer.write_u8(self.minor)?;
        Ok(())
    }
}

/// This trait is assigned to all versions of a versioned type and is the only means of
/// serialization/deserialization we use.
///
/// It also allows versions to serialize/deserialize themselves differently on a per-version basis.
/// Doing this here enforces consistency at a given filesystem version.
///
pub trait Versioned: Serialize + for<'de> Deserialize<'de> {
    fn max_serialized_size() -> u64 {
        DEFAULT_MAX_SERIALIZED_RECORD_SIZE
    }

    fn deserialize_from<R: ?Sized>(reader: &mut R, _version: Version) -> anyhow::Result<Self>
    where
        R: std::io::Read,
        for<'de> Self: serde::Deserialize<'de>,
    {
        use bincode::Options;
        let options = bincode::DefaultOptions::new()
            .with_limit(Self::max_serialized_size())
            .allow_trailing_bytes();
        match options.deserialize_from(reader) {
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
        use bincode::Options;
        let options = bincode::DefaultOptions::new()
            .with_limit(Self::max_serialized_size())
            .allow_trailing_bytes();
        match options.serialize_into(writer, self) {
            // Strip bincode wrapping. anyhow can take std::io::Error.
            Err(e) => Err(if let bincode::ErrorKind::Io(e) = *e { e.into() } else { e.into() }),
            Ok(t) => Ok(t),
        }
    }
}

/// This trait is only assigned to the latest version of a type and allows the type to deserialize
/// any older versions and upgrade them to the latest format.
pub trait VersionedLatest: Versioned + TypeHash {
    /// Deserializes from a given version format and upgrades to the latest version.
    fn deserialize_from_version<R>(reader: &mut R, version: Version) -> anyhow::Result<Self>
    where
        R: std::io::Read,
        Self: Sized;

    /// Like `deserialize_from_version` but reads Version from reader first, then uses it to
    /// deserialize self.
    fn deserialize_with_version<R>(reader: &mut R) -> anyhow::Result<(Self, Version)>
    where
        R: std::io::Read,
        Self: Sized,
    {
        let version = Version::deserialize_from(reader)?;
        Ok((Self::deserialize_from_version(reader, version)?, version))
    }
    /// Like `serialize_into` but serialized Version first, then self.
    fn serialize_with_version<W>(&self, writer: &mut W) -> anyhow::Result<()>
    where
        W: std::io::Write,
        Self: Sized,
    {
        LATEST_VERSION.serialize_into(writer)?;
        self.serialize_into(writer)
    }
}
