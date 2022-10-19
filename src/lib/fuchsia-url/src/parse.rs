// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::errors::{PackagePathSegmentError, ResourcePathError},
    serde::{Deserialize, Serialize},
    std::convert::{TryFrom, TryInto as _},
};

pub const MAX_PACKAGE_PATH_SEGMENT_BYTES: usize = 255;
pub const MAX_RESOURCE_PATH_SEGMENT_BYTES: usize = 255;

/// Check if a string conforms to r"^[0-9a-z\-\._]{1,255}$" and is neither "." nor ".."
pub fn validate_package_path_segment(string: &str) -> Result<(), PackagePathSegmentError> {
    if string.is_empty() {
        return Err(PackagePathSegmentError::Empty);
    }
    if string.len() > MAX_PACKAGE_PATH_SEGMENT_BYTES {
        return Err(PackagePathSegmentError::TooLong(string.len()));
    }
    if let Some(invalid_byte) = string.bytes().find(|&b| {
        !(b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'-' || b == b'.' || b == b'_')
    }) {
        return Err(PackagePathSegmentError::InvalidCharacter { character: invalid_byte.into() });
    }
    if string == "." {
        return Err(PackagePathSegmentError::DotSegment);
    }
    if string == ".." {
        return Err(PackagePathSegmentError::DotDotSegment);
    }

    Ok(())
}

/// A Fuchsia Package Name. Package names are the first segment of the path.
/// https://fuchsia.dev/fuchsia-src/concepts/packages/package_url#package-name
#[derive(PartialEq, Eq, PartialOrd, Ord, Debug, Clone, Hash, Serialize)]
pub struct PackageName(String);

impl std::str::FromStr for PackageName {
    type Err = PackagePathSegmentError;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let () = validate_package_path_segment(s)?;
        Ok(Self(s.into()))
    }
}

impl TryFrom<String> for PackageName {
    type Error = PackagePathSegmentError;
    fn try_from(value: String) -> Result<Self, Self::Error> {
        let () = validate_package_path_segment(&value)?;
        Ok(Self(value))
    }
}

impl From<PackageName> for String {
    fn from(name: PackageName) -> Self {
        name.0
    }
}

impl From<&PackageName> for String {
    fn from(name: &PackageName) -> Self {
        name.0.clone()
    }
}

impl std::fmt::Display for PackageName {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl AsRef<str> for PackageName {
    fn as_ref(&self) -> &str {
        &self.0
    }
}

impl<'de> Deserialize<'de> for PackageName {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        let value = String::deserialize(deserializer)?;
        value
            .try_into()
            .map_err(|e| serde::de::Error::custom(format!("invalid package name: {}", e)))
    }
}

/// A Fuchsia Package Variant. Package variants are the optional second segment of the path.
#[derive(PartialEq, Eq, PartialOrd, Ord, Debug, Clone, Hash, Serialize)]
pub struct PackageVariant(String);

impl PackageVariant {
    /// Create a `PackageVariant` of "0".
    pub fn zero() -> Self {
        "0".parse().expect("\"0\" is a valid variant")
    }

    /// Returns true iff the variant is "0".
    pub fn is_zero(&self) -> bool {
        self.0 == "0"
    }
}

impl std::str::FromStr for PackageVariant {
    type Err = PackagePathSegmentError;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let () = validate_package_path_segment(s)?;
        Ok(Self(s.into()))
    }
}

impl TryFrom<String> for PackageVariant {
    type Error = PackagePathSegmentError;
    fn try_from(value: String) -> Result<Self, Self::Error> {
        let () = validate_package_path_segment(&value)?;
        Ok(Self(value))
    }
}

impl std::fmt::Display for PackageVariant {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl AsRef<str> for PackageVariant {
    fn as_ref(&self) -> &str {
        &self.0
    }
}

impl<'de> Deserialize<'de> for PackageVariant {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        let value = String::deserialize(deserializer)?;
        value
            .try_into()
            .map_err(|e| serde::de::Error::custom(format!("invalid package variant {}", e)))
    }
}

/// Checks if `input` is a valid resource path for a Fuchsia Package URL.
/// Fuchsia package resource paths are Fuchsia object relative paths without
/// the limit on maximum path length.
/// https://fuchsia.dev/fuchsia-src/concepts/packages/package_url#resource-path
pub fn validate_resource_path(input: &str) -> Result<(), ResourcePathError> {
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
        if segment.len() > MAX_RESOURCE_PATH_SEGMENT_BYTES {
            return Err(ResourcePathError::NameTooLong);
        }
        // TODO(fxbug.dev/22531) allow newline once meta/contents supports it in blob paths
        if segment.contains('\n') {
            return Err(ResourcePathError::NameContainsNewline);
        }
    }
    Ok(())
}

#[cfg(test)]
mod test_validate_package_path_segment {
    use {super::*, crate::test::random_package_segment, proptest::prelude::*};

    #[test]
    fn reject_empty_segment() {
        assert_eq!(validate_package_path_segment(""), Err(PackagePathSegmentError::Empty));
    }

    #[test]
    fn reject_dot_segment() {
        assert_eq!(validate_package_path_segment("."), Err(PackagePathSegmentError::DotSegment));
    }

    #[test]
    fn reject_dot_dot_segment() {
        assert_eq!(
            validate_package_path_segment(".."),
            Err(PackagePathSegmentError::DotDotSegment)
        );
    }

    proptest! {
        #![proptest_config(ProptestConfig{
            failure_persistence: None,
            ..Default::default()
        })]

        #[test]
        fn reject_segment_too_long(ref s in r"[-_0-9a-z\.]{256, 300}")
        {
            prop_assert_eq!(
                validate_package_path_segment(s),
                Err(PackagePathSegmentError::TooLong(s.len()))
            );
        }

        #[test]
        fn reject_invalid_character(ref s in r"[-_0-9a-z\.]{0, 48}[^-_0-9a-z\.][-_0-9a-z\.]{0, 48}")
        {
            let pass = matches!(
                validate_package_path_segment(s),
                Err(PackagePathSegmentError::InvalidCharacter{..})
            );
            prop_assert!(pass);
        }

        #[test]
        fn valid_segment(ref s in random_package_segment())
        {
            prop_assert_eq!(
                validate_package_path_segment(s),
                Ok(())
            );
        }
    }
}

#[cfg(test)]
mod test_package_name {
    use super::*;

    #[test]
    fn from_str_rejects_invalid() {
        assert_eq!(
            "?".parse::<PackageName>(),
            Err(PackagePathSegmentError::InvalidCharacter { character: '?'.into() })
        );
    }

    #[test]
    fn from_str_succeeds() {
        "package-name".parse::<PackageName>().unwrap();
    }

    #[test]
    fn try_from_rejects_invalid() {
        assert_eq!(
            PackageName::try_from("?".to_string()),
            Err(PackagePathSegmentError::InvalidCharacter { character: '?'.into() })
        );
    }

    #[test]
    fn try_from_succeeds() {
        PackageName::try_from("valid-name".to_string()).unwrap();
    }

    #[test]
    fn from_succeeds() {
        assert_eq!(
            String::from("package-name".parse::<PackageName>().unwrap()),
            "package-name".to_string()
        );
    }

    #[test]
    fn display() {
        let path: PackageName = "package-name".parse().unwrap();
        assert_eq!(format!("{}", path), "package-name");
    }

    #[test]
    fn as_ref() {
        let path: PackageName = "package-name".parse().unwrap();
        assert_eq!(path.as_ref(), "package-name");
    }

    #[test]
    fn deserialize_success() {
        let actual_value =
            serde_json::from_str::<PackageName>("\"package-name\"").expect("json to deserialize");
        assert_eq!(actual_value, "package-name".parse::<PackageName>().unwrap());
    }

    #[test]
    fn deserialize_rejects_invalid() {
        let msg = serde_json::from_str::<PackageName>("\"pack!age-name\"").unwrap_err().to_string();
        assert!(msg.contains("invalid package name"), r#"Bad error message: "{}""#, msg);
    }
}

#[cfg(test)]
mod test_package_variant {
    use super::*;

    #[test]
    fn zero() {
        assert_eq!(PackageVariant::zero().as_ref(), "0");
        assert!(PackageVariant::zero().is_zero());
        assert_eq!("1".parse::<PackageVariant>().unwrap().is_zero(), false);
    }

    #[test]
    fn from_str_rejects_invalid() {
        assert_eq!(
            "?".parse::<PackageVariant>(),
            Err(PackagePathSegmentError::InvalidCharacter { character: '?'.into() })
        );
    }

    #[test]
    fn from_str_succeeds() {
        "package-variant".parse::<PackageVariant>().unwrap();
    }

    #[test]
    fn try_from_rejects_invalid() {
        assert_eq!(
            PackageVariant::try_from("?".to_string()),
            Err(PackagePathSegmentError::InvalidCharacter { character: '?'.into() })
        );
    }

    #[test]
    fn try_from_succeeds() {
        PackageVariant::try_from("valid-variant".to_string()).unwrap();
    }

    #[test]
    fn display() {
        let path: PackageVariant = "package-variant".parse().unwrap();
        assert_eq!(format!("{}", path), "package-variant");
    }

    #[test]
    fn as_ref() {
        let path: PackageVariant = "package-variant".parse().unwrap();
        assert_eq!(path.as_ref(), "package-variant");
    }

    #[test]
    fn deserialize_success() {
        let actual_value = serde_json::from_str::<PackageVariant>("\"package-variant\"")
            .expect("json to deserialize");
        assert_eq!(actual_value, "package-variant".parse::<PackageVariant>().unwrap());
    }

    #[test]
    fn deserialize_rejects_invalid() {
        let msg =
            serde_json::from_str::<PackageVariant>("\"pack!age-variant\"").unwrap_err().to_string();
        assert!(msg.contains("invalid package variant"), r#"Bad error message: "{}""#, msg);
    }
}

#[cfg(test)]
mod test_validate_resource_path {
    use {super::*, crate::test::*, proptest::prelude::*};

    // Tests for invalid paths
    #[test]
    fn test_empty_string() {
        assert_eq!(validate_resource_path(""), Err(ResourcePathError::PathIsEmpty));
    }

    proptest! {
        #![proptest_config(ProptestConfig{
            failure_persistence: None,
            ..Default::default()
        })]

        #[test]
        fn test_reject_empty_object_name(
            ref s in random_resource_path_with_regex_segment_str(5, "")) {
            prop_assume!(!s.starts_with('/') && !s.ends_with('/'));
            prop_assert_eq!(validate_resource_path(s), Err(ResourcePathError::NameEmpty));
        }

        #[test]
        fn test_reject_long_object_name(
            ref s in random_resource_path_with_regex_segment_str(5, r"[[[:ascii:]]--\.--/--\x00]{256}")) {
            prop_assert_eq!(validate_resource_path(s), Err(ResourcePathError::NameTooLong));
        }

        #[test]
        fn test_reject_contains_null(
            ref s in random_resource_path_with_regex_segment_string(
                5, format!(r"{}{{0,3}}\x00{}{{0,3}}",
                           ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE,
                           ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE))) {
            prop_assert_eq!(validate_resource_path(s), Err(ResourcePathError::NameContainsNull));
        }

        #[test]
        fn test_reject_name_is_dot(
            ref s in random_resource_path_with_regex_segment_str(5, r"\.")) {
            prop_assert_eq!(validate_resource_path(s), Err(ResourcePathError::NameIsDot));
        }

        #[test]
        fn test_reject_name_is_dot_dot(
            ref s in random_resource_path_with_regex_segment_str(5, r"\.\.")) {
            prop_assert_eq!(validate_resource_path(s), Err(ResourcePathError::NameIsDotDot));
        }

        #[test]
        fn test_reject_starts_with_slash(
            ref s in format!(
                "/{}{{1,5}}",
                ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE).as_str()) {
            prop_assert_eq!(validate_resource_path(s), Err(ResourcePathError::PathStartsWithSlash));
        }

        #[test]
        fn test_reject_ends_with_slash(
            ref s in format!(
                "{}{{1,5}}/",
                ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE).as_str()) {
            prop_assert_eq!(validate_resource_path(s), Err(ResourcePathError::PathEndsWithSlash));
        }

        #[test]
        fn test_reject_contains_newline(
            ref s in random_resource_path_with_regex_segment_string(
                5, format!(r"{}{{0,3}}\x0a{}{{0,3}}",
                           ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE,
                           ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE))) {
            prop_assert_eq!(validate_resource_path(s), Err(ResourcePathError::NameContainsNewline));
        }
    }

    // Tests for valid paths
    proptest! {
        #![proptest_config(ProptestConfig{
            failure_persistence: None,
            ..Default::default()
        })]

        #[test]
        fn test_name_contains_dot(
            ref s in random_resource_path_with_regex_segment_string(
                5, format!(r"{}{{1,4}}\.{}{{1,4}}",
                           ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE,
                           ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE)))
        {
            prop_assert_eq!(validate_resource_path(s), Ok(()));
        }

        #[test]
        fn test_name_contains_dot_dot(
            ref s in random_resource_path_with_regex_segment_string(
                5, format!(r"{}{{1,4}}\.\.{}{{1,4}}",
                           ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE,
                           ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE)))
        {
            prop_assert_eq!(validate_resource_path(s), Ok(()));
        }

        #[test]
        fn test_single_segment(ref s in always_valid_resource_path_chars(1, 4)) {
            prop_assert_eq!(validate_resource_path(s), Ok(()));
        }

        #[test]
        fn test_multi_segment(
            ref s in prop::collection::vec(always_valid_resource_path_chars(1, 4), 1..5))
        {
            let path = s.join("/");
            prop_assert_eq!(validate_resource_path(&path), Ok(()));
        }

        #[test]
        fn test_long_name(
            ref s in random_resource_path_with_regex_segment_str(
                5, "[[[:ascii:]]--\0--/--\n]{255}")) // TODO(fxbug.dev/22531) allow newline once meta/contents supports it in blob paths
        {
            prop_assert_eq!(validate_resource_path(s), Ok(()));
        }
    }
}
