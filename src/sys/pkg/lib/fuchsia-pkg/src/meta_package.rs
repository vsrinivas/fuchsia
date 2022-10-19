// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{errors::MetaPackageError, path::PackagePath},
    fuchsia_url::{PackageName, PackageVariant},
    serde::{Deserialize, Serialize},
    std::{convert::TryInto as _, io},
};

/// A `MetaPackage` represents the "meta/package" file of a meta.far (which is
/// a Fuchsia archive file of a Fuchsia package).
/// It validates that the name and variant (called "version" in json) are valid.
#[derive(Debug, Eq, PartialEq, Clone)]
pub struct MetaPackage {
    name: PackageName,
    variant: PackageVariant,
}

impl MetaPackage {
    /// Create a `MetaPackage` with `name`.
    pub fn from_name(name: PackageName) -> Self {
        Self { name, variant: PackageVariant::zero() }
    }

    /// Returns the package's name.
    pub fn name(&self) -> &PackageName {
        &self.name
    }

    /// Returns the package's variant.
    pub fn variant(&self) -> &PackageVariant {
        &self.variant
    }

    /// Convert into PackagePath.
    pub fn into_path(self) -> PackagePath {
        PackagePath::from_name_and_variant(self.name, self.variant)
    }

    /// Deserializes a `MetaPackage` from json.
    ///
    /// # Examples
    /// ```
    /// # use fuchsia_pkg::MetaPackage;
    /// let json = r#"
    ///     {"name": "package-name",
    ///      "version": "0"}"#;
    /// assert_eq!(
    ///     MetaPackage::deserialize(json.as_bytes()).unwrap(),
    ///     MetaPackage::from_name(
    ///         "package-name".parse().unwrap(),
    ///         "0".parse().unwrap(),
    ///     )
    /// );
    /// ```
    pub fn deserialize(reader: impl io::BufRead) -> Result<Self, MetaPackageError> {
        MetaPackage::from_v0(serde_json::from_reader(reader)?)
    }

    /// Serializes a `MetaPackage` to json.
    ///
    /// # Examples
    /// ```
    /// # use fuchsia_pkg::MetaPackage;
    /// let meta_package = MetaPackage::from_name("package-name".parse().unwrap());
    /// let mut v: Vec<u8> = vec![];
    /// meta_package.serialize(&mut v).unwrap();
    /// assert_eq!(std::str::from_utf8(v.as_slice()).unwrap(),
    ///            r#"{"name":"package-name","version":"0"}"#);
    /// ```
    pub fn serialize(&self, writer: impl io::Write) -> Result<(), MetaPackageError> {
        serde_json::to_writer(
            writer,
            &MetaPackageV0Serialize { name: self.name.as_ref(), variant: self.variant.as_ref() },
        )?;
        Ok(())
    }

    fn from_v0(v0: MetaPackageV0Deserialize) -> Result<MetaPackage, MetaPackageError> {
        Ok(MetaPackage {
            name: v0.name.try_into().map_err(MetaPackageError::PackageName)?,
            variant: v0.variant.try_into().map_err(MetaPackageError::PackageVariant)?,
        })
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
struct MetaPackageV0Deserialize {
    name: String,
    #[serde(rename = "version")]
    variant: String,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
struct MetaPackageV0Serialize<'a> {
    name: &'a str,
    #[serde(rename = "version")]
    variant: &'a str,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test::*;
    use lazy_static::lazy_static;
    use proptest::prelude::*;
    use regex::Regex;

    #[test]
    fn test_accessors() {
        let meta_package = MetaPackage::from_name("foo".parse().unwrap());
        assert_eq!(meta_package.name(), &"foo".parse::<PackageName>().unwrap());
        assert_eq!(meta_package.variant(), &"0".parse::<PackageVariant>().unwrap());
    }

    #[test]
    fn test_serialize() {
        let meta_package = MetaPackage::from_name("package-name".parse().unwrap());
        let mut v: Vec<u8> = Vec::new();

        meta_package.serialize(&mut v).unwrap();

        let expected = r#"{"name":"package-name","version":"0"}"#;
        assert_eq!(v.as_slice(), expected.as_bytes());
    }

    #[test]
    fn test_deserialize() {
        let json_bytes = r#"{"name":"package-name","version":"0"}"#.as_bytes();
        assert_eq!(
            MetaPackage::deserialize(json_bytes).unwrap(),
            MetaPackage { name: "package-name".parse().unwrap(), variant: "0".parse().unwrap() }
        );
    }

    proptest! {
        #![proptest_config(ProptestConfig{
            failure_persistence: None,
            ..Default::default()
        })]

        #[test]
        fn test_serialize_deserialize_is_identity(
            meta_package in random_meta_package(),
        ) {
            let mut v: Vec<u8> = Vec::new();
            meta_package.serialize(&mut v).unwrap();
            let meta_package_round_trip = MetaPackage::deserialize(v.as_slice()).unwrap();
            assert_eq!(meta_package, meta_package_round_trip);
        }

        #[test]
        fn test_serialized_contains_no_whitespace(
            meta_package in random_meta_package(),
        ) {
            lazy_static! {
                static ref RE: Regex = Regex::new(r"(\p{White_Space})").unwrap();
            }
            let mut v: Vec<u8> = Vec::new();
            meta_package.serialize(&mut v).unwrap();
            assert!(!RE.is_match(std::str::from_utf8(v.as_slice()).unwrap()));
        }
    }
}
