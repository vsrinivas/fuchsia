// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::ParsePackagePathError;
pub use fuchsia_url::{PackageName, PackageVariant, MAX_PACKAGE_PATH_SEGMENT_BYTES};

/// A Fuchsia Package Path. Paths must currently be "{name}/{variant}".
#[derive(PartialEq, Eq, PartialOrd, Ord, Debug, Clone, Hash)]
pub struct PackagePath {
    name: PackageName,
    variant: PackageVariant,
}

impl PackagePath {
    pub const MAX_NAME_BYTES: usize = MAX_PACKAGE_PATH_SEGMENT_BYTES;
    pub const MAX_VARIANT_BYTES: usize = MAX_PACKAGE_PATH_SEGMENT_BYTES;

    pub fn from_name_and_variant(name: PackageName, variant: PackageVariant) -> Self {
        Self { name, variant }
    }

    pub fn name(&self) -> &PackageName {
        &self.name
    }

    pub fn variant(&self) -> &PackageVariant {
        &self.variant
    }

    pub fn into_name_and_variant(self) -> (PackageName, PackageVariant) {
        (self.name, self.variant)
    }
}

impl std::str::FromStr for PackagePath {
    type Err = ParsePackagePathError;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let (name, variant_with_leading_slash) = match (s.find('/'), s.rfind('/')) {
            (Option::Some(l), Option::Some(r)) if l == r => s.split_at(l),
            (Option::Some(_), Option::Some(_)) => {
                return Err(Self::Err::TooManySegments);
            }
            _ => {
                return Err(Self::Err::TooFewSegments);
            }
        };
        Ok(Self::from_name_and_variant(
            name.parse().map_err(ParsePackagePathError::PackageName)?,
            variant_with_leading_slash[1..]
                .parse()
                .map_err(ParsePackagePathError::PackageVariant)?,
        ))
    }
}

impl std::fmt::Display for PackagePath {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}/{}", self.name, self.variant)
    }
}

#[cfg(test)]
mod test {
    use {
        super::*, crate::test::random_package_path, fuchsia_url::errors::PackagePathSegmentError,
        proptest::prelude::*,
    };

    #[test]
    fn reject_invalid_name() {
        let res: Result<PackagePath, _> = "/0".parse();
        assert_eq!(res, Err(ParsePackagePathError::PackageName(PackagePathSegmentError::Empty)));
    }

    #[test]
    fn reject_invalid_variant() {
        let res: Result<PackagePath, _> = "valid_name/".parse();
        assert_eq!(res, Err(ParsePackagePathError::PackageVariant(PackagePathSegmentError::Empty)));
    }

    #[test]
    fn display() {
        assert_eq!(
            format!(
                "{}",
                PackagePath::from_name_and_variant(
                    "package-name".parse().unwrap(),
                    "package-variant".parse().unwrap()
                )
            ),
            "package-name/package-variant"
        );
    }

    #[test]
    fn accessors() {
        let name = "package-name".parse::<PackageName>().unwrap();
        let variant = "package-variant".parse::<PackageVariant>().unwrap();
        let path = PackagePath::from_name_and_variant(name.clone(), variant.clone());
        assert_eq!(path.name(), &name);
        assert_eq!(path.variant(), &variant);
    }

    proptest! {
        #![proptest_config(ProptestConfig{
            failure_persistence: None,
            ..Default::default()
        })]

        #[test]
        fn display_from_str_round_trip(path in random_package_path()) {
            prop_assert_eq!(
                path.clone(),
                path.to_string().parse().unwrap()
            );
        }
    }
}
