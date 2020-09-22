// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::{
    PackageNameError, PackagePathError, PackageVariantError, ParsePackagePathError,
    ResourcePathError,
};
use lazy_static::lazy_static;
use regex::Regex;

pub const MAX_OBJECT_BYTES: usize = 255;
pub const MAX_PACKAGE_NAME_BYTES: usize = 100;
pub const MAX_PACKAGE_VARIANT_BYTES: usize = 100;
// FIXME(PKG-757): '_' is not valid in package names, but many Fuchsia packages currently use that
// character.
pub const PACKAGE_NAME_REGEX: &str = r"^[-_0-9a-z\.]{1, 100}$";
pub const PACKAGE_VARIANT_REGEX: &str = r"^[-0-9a-z\.]{1, 100}$";

/// Checks if `input` is a valid path for a file in a Fuchsia package.
/// Fuchsia package resource paths are Fuchsia object relative paths without
/// the limit on maximum path length.
/// Passes the input through if it is valid.
pub fn check_resource_path(input: &str) -> Result<&str, ResourcePathError> {
    if input.is_empty() {
        return Err(ResourcePathError::PathIsEmpty);
    }
    if input.starts_with('/') {
        return Err(ResourcePathError::PathStartsWithSlash);
    }
    if input.ends_with('/') {
        return Err(ResourcePathError::PathEndsWithSlash);
    }
    for segment in input.split('/') {
        if segment.contains('\0') {
            return Err(ResourcePathError::NameContainsNull);
        }
        if segment == "." {
            return Err(ResourcePathError::NameIsDot);
        }
        if segment == ".." {
            return Err(ResourcePathError::NameIsDotDot);
        }
        if segment.is_empty() {
            return Err(ResourcePathError::NameEmpty);
        }
        if segment.len() > MAX_OBJECT_BYTES {
            return Err(ResourcePathError::NameTooLong);
        }
        // TODO(fxbug.dev/22531) allow newline once meta/contents supports it in blob paths
        if segment.contains('\n') {
            return Err(ResourcePathError::NameContainsNewline);
        }
    }
    Ok(input)
}

/// Checks if `input` is a valid Fuchsia package name.
/// Passes `input` through if valid.
pub fn check_package_name(input: &str) -> Result<&str, PackageNameError> {
    if input.len() > MAX_PACKAGE_NAME_BYTES {
        return Err(PackageNameError::TooLong { invalid_name: input.to_string() });
    }
    if input.is_empty() {
        return Err(PackageNameError::Empty);
    }
    lazy_static! {
        static ref RE: Regex = Regex::new(PACKAGE_NAME_REGEX).unwrap();
    }
    if !RE.is_match(input) {
        return Err(PackageNameError::InvalidCharacter { invalid_name: input.to_string() });
    }
    Ok(input)
}

/// Checks if `input` is a valid Fuchsia package variant.
/// Passes `input` through if valid.
pub fn check_package_variant(input: &str) -> Result<&str, PackageVariantError> {
    if input.len() > MAX_PACKAGE_VARIANT_BYTES {
        return Err(PackageVariantError::TooLong { invalid_variant: input.to_string() });
    }
    if input.is_empty() {
        return Err(PackageVariantError::Empty);
    }
    lazy_static! {
        static ref RE: Regex = Regex::new(PACKAGE_VARIANT_REGEX).unwrap();
    }
    if !RE.is_match(input) {
        return Err(PackageVariantError::InvalidCharacter { invalid_variant: input.to_string() });
    }
    Ok(input)
}

/// A Fuchsia Package Path. Paths must currently be "{name}/{variant}".
#[derive(PartialEq, Eq, Debug, Clone)]
pub struct PackagePath {
    name: String,
    variant: String,
}

impl PackagePath {
    pub fn from_name_and_variant(
        name: impl Into<String>,
        variant: impl Into<String>,
    ) -> Result<Self, PackagePathError> {
        let name = name.into();
        check_package_name(&name)?;
        let variant = variant.into();
        check_package_variant(&variant)?;
        Ok(Self { name, variant })
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn variant(&self) -> &str {
        &self.variant
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
        Ok(Self::from_name_and_variant(name, &variant_with_leading_slash[1..])?)
    }
}

impl std::fmt::Display for PackagePath {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}/{}", self.name, self.variant)
    }
}

#[cfg(test)]
mod check_resource_path_tests {
    use super::*;
    use crate::test::*;
    use proptest::prelude::*;

    // Tests for invalid paths
    #[test]
    fn test_empty_string() {
        assert_eq!(check_resource_path(""), Err(ResourcePathError::PathIsEmpty));
    }

    proptest! {
        #[test]
        fn test_reject_empty_object_name(
            ref s in random_resource_path_with_regex_segment_str(5, "")) {
            prop_assume!(!s.starts_with('/') && !s.ends_with('/'));
            prop_assert_eq!(check_resource_path(s), Err(ResourcePathError::NameEmpty));
        }

        #[test]
        fn test_reject_long_object_name(
            ref s in random_resource_path_with_regex_segment_str(5, r"[[[:ascii:]]--\.--/--\x00]{256}")) {
            prop_assert_eq!(check_resource_path(s), Err(ResourcePathError::NameTooLong));
        }

        #[test]
        fn test_reject_contains_null(
            ref s in random_resource_path_with_regex_segment_string(
                5, format!(r"{}{{0,3}}\x00{}{{0,3}}",
                           ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE,
                           ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE))) {
            prop_assert_eq!(check_resource_path(s), Err(ResourcePathError::NameContainsNull));
        }

        #[test]
        fn test_reject_name_is_dot(
            ref s in random_resource_path_with_regex_segment_str(5, r"\.")) {
            prop_assert_eq!(check_resource_path(s), Err(ResourcePathError::NameIsDot));
        }

        #[test]
        fn test_reject_name_is_dot_dot(
            ref s in random_resource_path_with_regex_segment_str(5, r"\.\.")) {
            prop_assert_eq!(check_resource_path(s), Err(ResourcePathError::NameIsDotDot));
        }

        #[test]
        fn test_reject_starts_with_slash(
            ref s in format!(
                "/{}{{1,5}}",
                ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE).as_str()) {
            prop_assert_eq!(check_resource_path(s), Err(ResourcePathError::PathStartsWithSlash));
        }

        #[test]
        fn test_reject_ends_with_slash(
            ref s in format!(
                "{}{{1,5}}/",
                ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE).as_str()) {
            prop_assert_eq!(check_resource_path(s), Err(ResourcePathError::PathEndsWithSlash));
        }

        #[test]
        fn test_reject_contains_newline(
            ref s in random_resource_path_with_regex_segment_string(
                5, format!(r"{}{{0,3}}\x0a{}{{0,3}}",
                           ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE,
                           ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE))) {
            prop_assert_eq!(check_resource_path(s), Err(ResourcePathError::NameContainsNewline));
        }
    }

    // Tests for valid paths
    proptest! {
        #[test]
        fn test_name_contains_dot(
            ref s in random_resource_path_with_regex_segment_string(
                5, format!(r"{}{{1,4}}\.{}{{1,4}}",
                           ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE,
                           ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE)))
        {
            prop_assert_eq!(check_resource_path(s), Ok(s.as_str()));
        }

        #[test]
        fn test_name_contains_dot_dot(
            ref s in random_resource_path_with_regex_segment_string(
                5, format!(r"{}{{1,4}}\.\.{}{{1,4}}",
                           ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE,
                           ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE)))
        {
            prop_assert_eq!(check_resource_path(s), Ok(s.as_str()));
        }

        #[test]
        fn test_single_segment(ref s in always_valid_resource_path_chars(1, 4)) {
            prop_assert_eq!(check_resource_path(s), Ok(s.as_str()));
        }

        #[test]
        fn test_multi_segment(
            ref s in prop::collection::vec(always_valid_resource_path_chars(1, 4), 1..5))
        {
            let path = s.join("/");
            prop_assert_eq!(check_resource_path(&path), Ok(path.as_str()));
        }

        #[test]
        fn test_long_name(
            ref s in random_resource_path_with_regex_segment_str(
                5, "[[[:ascii:]]--\0--/--\n]{255}")) // TODO(fxbug.dev/22531) allow newline once meta/contents supports it in blob paths
        {
            prop_assert_eq!(check_resource_path(s), Ok(s.as_str()));
        }
    }
}

#[cfg(test)]
mod check_package_name_tests {
    use super::*;
    use crate::test::random_package_name;
    use proptest::prelude::*;

    #[test]
    fn test_reject_empty_name() {
        assert_eq!(check_package_name(""), Err(PackageNameError::Empty));
    }

    proptest! {
        #[test]
        fn test_reject_name_too_long(ref s in r"[-0-9a-z\.]{101, 200}")
        {
            prop_assert_eq!(
                check_package_name(s),
                Err(PackageNameError::TooLong{invalid_name: s.to_string()})
            );
        }

        // FIXME(PKG-757): '_' should be considered to be an invalid character.
        #[test]
        fn test_reject_invalid_character(ref s in r"[-0-9a-z\.]{0, 48}[^-_0-9a-z\.][-0-9a-z\.]{0, 48}")
        {
            prop_assert_eq!(
                check_package_name(s),
                Err(PackageNameError::InvalidCharacter{invalid_name: s.to_string()})
            );
        }

        #[test]
        fn test_pass_through_valid_name(ref s in random_package_name())
        {
            prop_assert_eq!(
                check_package_name(s),
                Ok(s.as_str())
            );
        }
    }
}

#[cfg(test)]
mod check_package_variant_tests {
    use super::*;
    use crate::test::random_package_variant;
    use proptest::prelude::*;

    #[test]
    fn test_reject_empty_variant() {
        assert_eq!(check_package_variant(""), Err(PackageVariantError::Empty));
    }

    proptest! {
        #[test]
        fn test_reject_variant_too_long(ref s in r"[-0-9a-z\.]{101, 200}")
        {
            prop_assert_eq!(
                check_package_variant(s),
                Err(PackageVariantError::TooLong{invalid_variant: s.to_string()})
            );
        }

        #[test]
        fn test_reject_invalid_character(ref s in r"[-0-9a-z\.]{0, 48}[^-0-9a-z\.][-0-9a-z\.]{0, 48}")
        {
            prop_assert_eq!(
                check_package_variant(s),
                Err(PackageVariantError::InvalidCharacter{invalid_variant: s.to_string()})
            );
        }

        #[test]
        fn test_pass_through_valid_variant(ref s in random_package_variant())
        {
            prop_assert_eq!(
                check_package_variant(s),
                Ok(s.as_str())
            );
        }
    }
}

#[cfg(test)]
mod check_package_path_tests {
    use {super::*, crate::test::random_package_path, proptest::prelude::*};

    #[test]
    fn reject_invalid_name() {
        let res: Result<PackagePath, _> = "/0".parse();
        assert_eq!(
            res,
            Err(ParsePackagePathError::PackagePath(PackagePathError::PackageName(
                PackageNameError::Empty
            )))
        );
    }

    #[test]
    fn reject_invalid_variant() {
        let res: Result<PackagePath, _> = "valid_name/".parse();
        assert_eq!(
            res,
            Err(ParsePackagePathError::PackagePath(PackagePathError::PackageVariant(
                PackageVariantError::Empty
            )))
        );
    }

    #[test]
    fn display() {
        assert_eq!(
            format!(
                "{}",
                PackagePath::from_name_and_variant("package-name", "package-variant").unwrap()
            ),
            "package-name/package-variant"
        );
    }

    #[test]
    fn accessors() {
        let name = "package-name";
        let variant = "package-variant";
        let path = PackagePath::from_name_and_variant(name, variant).unwrap();
        assert_eq!(path.name(), name);
        assert_eq!(path.variant(), variant);
    }

    proptest! {
        #[test]
        fn display_from_str_round_trip(path in random_package_path()) {
            prop_assert_eq!(
                path.clone(),
                path.to_string().parse().unwrap()
            );
        }
    }
}
