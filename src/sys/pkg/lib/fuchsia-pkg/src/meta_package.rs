// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    errors::MetaPackageError,
    path::{check_package_name, check_package_variant, PackagePath},
};
use serde::{Deserialize, Serialize};
use std::io;

#[cfg(test)]
use proptest_derive::Arbitrary;

/// A `MetaPackage` represents the "meta/package" file of a meta.far (which is
/// a Fuchsia archive file of a Fuchsia package).
/// It validates that the name and variant (called "version" in json) are valid.
#[derive(Debug, Eq, PartialEq, Clone)]
#[cfg_attr(test, derive(Arbitrary))]
pub struct MetaPackage {
    #[cfg_attr(test, proptest(regex = r"[-0-9a-z\.]{1,100}"))]
    name: String,

    #[cfg_attr(test, proptest(regex = r"[-0-9a-z\.]{1,100}"))]
    variant: String,
}

impl MetaPackage {
    /// Create a `MetaPackage` with `name` and `variant`.
    pub fn from_name_and_variant(
        name: impl Into<String>,
        variant: impl Into<String>,
    ) -> Result<Self, MetaPackageError> {
        let name = name.into();
        let variant = variant.into();
        check_package_name(&name)?;
        check_package_variant(&variant)?;
        Ok(MetaPackage { name, variant })
    }

    /// Returns the package's name.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// Returns the package's variant.
    pub fn variant(&self) -> &str {
        &self.variant
    }

    /// Convert into PackagePath.
    pub fn into_path(self) -> PackagePath {
        // unwrap can not fail because the name and variant has already been validated.
        PackagePath::from_name_and_variant(self.name, self.variant).unwrap()
    }

    /// Deserializes a `MetaPackage` from json.
    ///
    /// # Examples
    /// ```
    /// # use fuchsia_pkg::MetaPackage;
    /// let json = r#"
    ///     {"name": "package-name",
    ///      "version": "package-variant"}"#;
    /// assert_eq!(
    ///     MetaPackage::deserialize(json.as_bytes()).unwrap(),
    ///     MetaPackage::from_name_and_variant(
    ///         "package-name",
    ///         "package-variant").unwrap());
    /// ```
    pub fn deserialize(reader: impl io::BufRead) -> Result<Self, MetaPackageError> {
        MetaPackage::from_v0(serde_json::from_reader(reader)?)
    }

    /// Serializes a `MetaPackage` to json.
    ///
    /// # Examples
    /// ```
    /// # use fuchsia_pkg::MetaPackage;
    /// let meta_package = MetaPackage::from_name_and_variant(
    ///         "package-name",
    ///         "package-variant").unwrap();
    /// let mut v: Vec<u8> = vec![];
    /// meta_package.serialize(&mut v).unwrap();
    /// assert_eq!(std::str::from_utf8(v.as_slice()).unwrap(),
    ///            r#"{"name":"package-name","version":"package-variant"}"#);
    /// ```
    pub fn serialize(&self, writer: impl io::Write) -> Result<(), MetaPackageError> {
        serde_json::to_writer(
            writer,
            &MetaPackageV0Serialize { name: &self.name, variant: &self.variant },
        )?;
        Ok(())
    }

    fn from_v0(v0: MetaPackageV0Deserialize) -> Result<MetaPackage, MetaPackageError> {
        MetaPackage::from_name_and_variant(v0.name, v0.variant)
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
    use crate::errors::{PackageNameError, PackageVariantError};
    use lazy_static::lazy_static;
    use matches::assert_matches;
    use proptest::prelude::*;
    use regex::Regex;

    #[test]
    fn test_reject_invalid_name() {
        assert_matches!(
            MetaPackage::from_name_and_variant("name-with-question-mark?", "valid-variant"),
            Err(MetaPackageError::PackageName(PackageNameError::InvalidCharacter {
                invalid_name
            })) if invalid_name == "name-with-question-mark?"
        );
    }

    #[test]
    fn test_reject_invalid_variant() {
        assert_matches!(
            MetaPackage::from_name_and_variant("valid-name", "variant-with-question-mark?"),
            Err(MetaPackageError::PackageVariant(PackageVariantError::InvalidCharacter {
                invalid_variant
            })) if invalid_variant == "variant-with-question-mark?"
        );
    }

    #[test]
    fn test_from_name_and_variant() {
        let name = "package-name";
        let variant = "package-variant";
        assert_eq!(
            MetaPackage::from_name_and_variant(name, variant).unwrap(),
            MetaPackage { name: name.to_string(), variant: variant.to_string() }
        );
    }

    #[test]
    fn test_accessors() {
        let meta_package = MetaPackage::from_name_and_variant("foo", "bar").unwrap();
        assert_eq!(meta_package.name(), "foo");
        assert_eq!(meta_package.variant(), "bar");
    }

    #[test]
    fn test_serialize() {
        let meta_package =
            MetaPackage::from_name_and_variant("package-name", "package-variant").unwrap();
        let mut v: Vec<u8> = Vec::new();

        meta_package.serialize(&mut v).unwrap();

        let expected = r#"{"name":"package-name","version":"package-variant"}"#;
        assert_eq!(v.as_slice(), expected.as_bytes());
    }

    #[test]
    fn test_deserialize() {
        let json_bytes = r#"{"name":"package-name","version":"package-variant"}"#.as_bytes();
        assert_eq!(
            MetaPackage::deserialize(json_bytes).unwrap(),
            MetaPackage {
                name: "package-name".to_string(),
                variant: "package-variant".to_string()
            }
        );
    }

    proptest! {
        #[test]
        fn test_serialize_deserialize_is_identity(
            meta_package: MetaPackage,
        ) {
            let mut v: Vec<u8> = Vec::new();
            meta_package.serialize(&mut v).unwrap();
            let meta_package_round_trip = MetaPackage::deserialize(v.as_slice()).unwrap();
            assert_eq!(meta_package, meta_package_round_trip);
        }

        #[test]
        fn test_serialized_contains_no_whitespace(
            meta_package: MetaPackage,
        ) {
            lazy_static! {
                static ref RE: Regex = Regex::new(r"(\p{White_Space})").unwrap();
            }
            let mut v: Vec<u8> = Vec::new();
            meta_package.serialize(&mut v).unwrap();
            assert!(!RE.is_match(std::str::from_utf8(v.as_slice()).unwrap()));
        }

        #[test]
        fn test_from_name_and_variant_no_error_on_valid_inputs(
            meta_package: MetaPackage,
        ) {
            let MetaPackage { name, variant } = meta_package;
            assert!(MetaPackage::from_name_and_variant(name, variant).is_ok());
        }
    }
}
