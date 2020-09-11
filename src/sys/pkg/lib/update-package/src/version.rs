// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around reading the version file.

use {
    ::version::Version as SemanticVersion,
    fidl_fuchsia_io::DirectoryProxy,
    serde::{
        de::{self, Visitor},
        Deserialize, Deserializer, Serialize, Serializer,
    },
    std::{convert::Infallible, fmt, str::FromStr},
    thiserror::Error,
};

/// An error encountered while reading the version.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum ReadVersionError {
    #[error("while opening the file: {0}")]
    OpenFile(#[from] io_util::node::OpenError),

    #[error("while reading the file: {0}")]
    ReadFile(#[from] io_util::file::ReadError),
}

struct SystemVersionVisitor;

impl<'de> Visitor<'de> for SystemVersionVisitor {
    type Value = SystemVersion;

    fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("a string")
    }

    fn visit_str<E>(self, v: &str) -> Result<Self::Value, E>
    where
        E: de::Error,
    {
        Ok(SystemVersion::from_str(v).unwrap())
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
/// Represents the version of an update package.
pub enum SystemVersion {
    /// An unrecognised version format.
    Opaque(String),
    /// A version of the format "a.b.c.d".
    Semantic(SemanticVersion),
}

impl SystemVersion {
    /// Returns true if this SystemVersion is an empty string.
    pub fn is_empty(&self) -> bool {
        if let SystemVersion::Opaque(value) = self {
            value == ""
        } else {
            false
        }
    }
}

impl PartialOrd for SystemVersion {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        let my_version = match &self {
            SystemVersion::Opaque(_) => return None,
            SystemVersion::Semantic(ver) => ver,
        };

        let other_version = match other {
            SystemVersion::Opaque(_) => return None,
            SystemVersion::Semantic(ver) => ver,
        };

        my_version.partial_cmp(other_version)
    }
}

// We have manual implementations of deserialize/serialize so that we can parse simple strings.
impl<'de> Deserialize<'de> for SystemVersion {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        deserializer.deserialize_str(SystemVersionVisitor)
    }
}

impl Serialize for SystemVersion {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match self {
            SystemVersion::Semantic(ref version) => serializer.serialize_str(&version.to_string()),
            SystemVersion::Opaque(ref string) => serializer.serialize_str(string),
        }
    }
}

impl FromStr for SystemVersion {
    type Err = Infallible;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let result = match SemanticVersion::from_str(s) {
            Ok(version) => SystemVersion::Semantic(version),
            Err(_) => SystemVersion::Opaque(s.to_owned()),
        };

        Ok(result)
    }
}

impl fmt::Display for SystemVersion {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SystemVersion::Opaque(ref string) => f.write_str(string),
            SystemVersion::Semantic(ref version) => version.fmt(f),
        }
    }
}

pub(crate) async fn read_version(
    proxy: &DirectoryProxy,
) -> Result<SystemVersion, ReadVersionError> {
    let file =
        io_util::directory::open_file(proxy, "version", fidl_fuchsia_io::OPEN_RIGHT_READABLE)
            .await?;
    let version_str = io_util::file::read_to_string(&file).await?;

    Ok(SystemVersion::from_str(&version_str).unwrap())
}

#[cfg(test)]
mod tests {
    use {super::*, crate::TestUpdatePackage, fuchsia_async as fasync, matches::assert_matches};

    #[fasync::run_singlethreaded(test)]
    async fn read_version_success_file_exists() {
        let p = TestUpdatePackage::new().add_file("version", "123").await;
        assert_eq!(
            p.version().await.unwrap(),
            SystemVersion::Semantic(SemanticVersion::from([123]))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_version_success_opaque() {
        let p = TestUpdatePackage::new().add_file("version", "2020-09-08T10:17:00+10:00").await;
        assert_eq!(
            p.version().await.unwrap(),
            SystemVersion::Opaque("2020-09-08T10:17:00+10:00".to_owned())
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_version_fail_file_does_not_exist() {
        let p = TestUpdatePackage::new();
        assert_matches!(read_version(p.proxy()).await, Err(ReadVersionError::OpenFile(_)));
    }

    #[test]
    fn test_deserialize_version() {
        let version: SystemVersion =
            serde_json::from_str(r#""not a real version number""#).unwrap();
        assert_eq!(version, SystemVersion::Opaque("not a real version number".to_owned()));
        let version: SystemVersion = serde_json::from_str(r#""1.2.3""#).unwrap();
        assert_eq!(version, SystemVersion::Semantic(SemanticVersion::from([1, 2, 3])));
    }

    #[test]
    fn test_serialize_version() {
        let version = SystemVersion::Opaque("not a real version number".to_owned());
        assert_eq!(
            serde_json::to_string(&version).unwrap(),
            r#""not a real version number""#.to_owned()
        );
        let version = SystemVersion::Semantic(SemanticVersion::from([1, 3, 4, 5]));
        assert_eq!(serde_json::to_string(&version).unwrap(), r#""1.3.4.5""#.to_owned());
    }

    #[test]
    fn test_version_order_both_opaque() {
        // We don't attempt to assign ordering to opaque versions.
        let a = SystemVersion::Opaque("version 1".to_string());
        let b = SystemVersion::Opaque("another version".to_string());
        assert_eq!(a < b, false);
        assert_eq!(a > b, false);
        assert_eq!(a >= b, false);
        assert_eq!(a <= b, false);
    }

    #[test]
    fn test_version_order_one_opaque() {
        let a = SystemVersion::Opaque("opaque".to_string());
        let b = SystemVersion::Semantic(SemanticVersion::from([1, 2, 3, 4]));
        assert_eq!(a < b, false);
        assert_eq!(a > b, false);
        assert_eq!(a >= b, false);
        assert_eq!(a <= b, false);
        assert_eq!(b < a, false);
        assert_eq!(b > a, false);
        assert_eq!(b >= a, false);
        assert_eq!(b <= a, false);
    }

    #[test]
    fn test_version_order_both_semantic() {
        let a = SystemVersion::Semantic(SemanticVersion::from([1, 2, 3, 4]));
        let b = SystemVersion::Semantic(SemanticVersion::from([1, 2, 3, 5]));

        assert_eq!(a < b, true);
        assert_eq!(a <= b, true);
        assert_eq!(a > b, false);
        assert_eq!(a >= b, false);
        assert_eq!(b < a, false);
        assert_eq!(b <= a, false);
        assert_eq!(b >= a, true);
        assert_eq!(b > a, true);
    }
}
