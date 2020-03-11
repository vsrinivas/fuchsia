// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A crate containing common Component Manager types used in Component Manifests
//! (`.cml` files and binary `.cm` files). These types come with `serde` serialization
//! and deserialization implementations that perform the required validation.

use {
    serde::de,
    serde_derive::Serialize,
    std::{borrow::Cow, fmt, str::FromStr},
    thiserror::Error,
    url,
};

/// A name that can refer to a component, collection, or other entity in the
/// Component Manifest.
#[derive(Serialize, Clone, Debug, Hash, PartialEq, Eq)]
pub struct Name(String);

/// The error representing a failed validation of a `Name` string.
#[derive(Debug, Error)]
pub enum NameValidationError {
    /// The name string is empty or greater than 100 characters in length.
    #[error("name must be non-empty and no more than 100 characters in length")]
    InvalidLength,
    /// The name string contains illegal characters. See [`Name::new`].
    #[error("name must only contain alpha-numeric characters or _-.")]
    MalformedName,
}

impl Name {
    /// Creates a `Name` from a `String`, returning an `Err` if the string
    /// fails validation. The string must be non-empty, no more than 100
    /// characters in length, and consist of one or more of the
    /// following characters: `a-z`, `0-9`, `_`, `.`, `-`.
    pub fn new(name: String) -> Result<Self, NameValidationError> {
        Self::from_str_impl(Cow::Owned(name))
    }

    fn from_str_impl(name: Cow<'_, str>) -> Result<Self, NameValidationError> {
        if name.is_empty() || name.len() > 100 {
            return Err(NameValidationError::InvalidLength);
        }
        let valid_fn = |c: char| c.is_ascii_alphanumeric() || c == '_' || c == '-' || c == '.';
        if !name.chars().all(valid_fn) {
            return Err(NameValidationError::MalformedName);
        }
        Ok(Self(name.into_owned()))
    }

    pub fn as_str(&self) -> &str {
        self.0.as_str()
    }
}

impl PartialEq<&str> for Name {
    fn eq(&self, o: &&str) -> bool {
        self.0 == *o
    }
}

impl fmt::Display for Name {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        <String as fmt::Display>::fmt(&self.0, f)
    }
}

impl FromStr for Name {
    type Err = NameValidationError;

    fn from_str(name: &str) -> Result<Self, Self::Err> {
        Self::from_str_impl(Cow::Borrowed(name))
    }
}

impl From<Name> for String {
    fn from(name: Name) -> String {
        name.0
    }
}

impl<'de> de::Deserialize<'de> for Name {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: de::Deserializer<'de>,
    {
        struct Visitor;

        impl<'de> de::Visitor<'de> for Visitor {
            type Value = Name;

            fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str(
                    "a non-empty string no more than 100 characters in \
                     length, containing only alpha-numeric characters \
                     or [_-.]",
                )
            }

            fn visit_str<E>(self, s: &str) -> Result<Self::Value, E>
            where
                E: de::Error,
            {
                s.parse().map_err(|err| match err {
                    NameValidationError::InvalidLength => E::invalid_length(
                        s.len(),
                        &"a non-empty name no more than 100 characters in length",
                    ),
                    NameValidationError::MalformedName => E::invalid_value(
                        de::Unexpected::Str(s),
                        &"a name containing only alpha-numeric characters or [_-.]",
                    ),
                })
            }
        }
        deserializer.deserialize_string(Visitor)
    }
}

/// A filesystem path.
#[derive(Serialize, Clone, Debug, PartialEq, Eq)]
pub struct Path(String);

/// The error representing a failed validation of a `Path` string.
#[derive(Debug, Error)]
pub enum PathValidationError {
    /// The path string is empty or greater than 1024 characters in length.
    #[error("path must be non-empty and no more than 1024 characters in length")]
    InvalidLength,
    /// The path string is malformed. See [`Path::new`].
    #[error("path is malformed")]
    MalformedPath,
}

impl Path {
    /// Creates a `Path` from a `String`, returning an `Err` if the string
    /// fails validation. The string must be non-empty, no more than 1024
    /// characters in length, start with a leading `/`, and contain no empty
    /// path segments.
    pub fn new(path: String) -> Result<Self, PathValidationError> {
        Self::validate(&path)?;
        Ok(Path(path))
    }

    /// Validates `path` but does not construct a new `Path` object.
    pub fn validate(path: &str) -> Result<(), PathValidationError> {
        if path.is_empty() || path.len() > 1024 {
            return Err(PathValidationError::InvalidLength);
        }
        if !path.starts_with('/') {
            return Err(PathValidationError::MalformedPath);
        }
        if !path[1..].split('/').all(|part| !part.is_empty()) {
            return Err(PathValidationError::MalformedPath);
        }
        Ok(())
    }

    pub fn as_str(&self) -> &str {
        self.0.as_str()
    }
}

impl FromStr for Path {
    type Err = PathValidationError;

    fn from_str(path: &str) -> Result<Self, Self::Err> {
        Self::validate(path)?;
        Ok(Path(path.to_string()))
    }
}

impl From<Path> for String {
    fn from(path: Path) -> String {
        path.0
    }
}

impl<'de> de::Deserialize<'de> for Path {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: de::Deserializer<'de>,
    {
        struct Visitor;

        impl<'de> de::Visitor<'de> for Visitor {
            type Value = Path;

            fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str(
                    "a non-empty path no more than 1024 characters \
                     in length, with a leading `/`, and containing no \
                     empty path segments",
                )
            }

            fn visit_str<E>(self, s: &str) -> Result<Self::Value, E>
            where
                E: de::Error,
            {
                s.parse().map_err(|err| match err {
                    PathValidationError::InvalidLength => E::invalid_length(
                        s.len(),
                        &"a non-empty path no more than 1024 characters in length",
                    ),
                    PathValidationError::MalformedPath => E::invalid_value(
                        de::Unexpected::Str(s),
                        &"a path with leading `/` and non-empty segments",
                    ),
                })
            }
        }
        deserializer.deserialize_string(Visitor)
    }
}

/// A relative filesystem path.
#[derive(Serialize, Clone, Debug, PartialEq, Eq)]
pub struct RelativePath(String);

impl RelativePath {
    /// Creates a `RelativePath` from a `String`, returning an `Err` if the string fails
    /// validation. The string must be non-empty, no more than 1024 characters in length, not start
    /// with a `/`, and contain no empty path segments.
    pub fn new(path: String) -> Result<Self, PathValidationError> {
        Self::from_str_impl(Cow::Owned(path))
    }

    fn from_str_impl(path: Cow<'_, str>) -> Result<Self, PathValidationError> {
        if path.is_empty() || path.len() > 1024 {
            return Err(PathValidationError::InvalidLength);
        }
        if !path.split('/').all(|part| !part.is_empty()) {
            return Err(PathValidationError::MalformedPath);
        }
        return Ok(Self(path.into_owned()));
    }

    pub fn as_str(&self) -> &str {
        self.0.as_str()
    }
}

impl FromStr for RelativePath {
    type Err = PathValidationError;

    fn from_str(path: &str) -> Result<Self, Self::Err> {
        Self::from_str_impl(Cow::Borrowed(path))
    }
}

impl From<RelativePath> for String {
    fn from(path: RelativePath) -> String {
        path.0
    }
}

impl<'de> de::Deserialize<'de> for RelativePath {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: de::Deserializer<'de>,
    {
        struct Visitor;

        impl<'de> de::Visitor<'de> for Visitor {
            type Value = RelativePath;

            fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str(
                    "a non-empty path no more than 1024 characters \
                     in length, not starting with `/`, and containing no \
                     empty path segments",
                )
            }

            fn visit_str<E>(self, s: &str) -> Result<Self::Value, E>
            where
                E: de::Error,
            {
                s.parse().map_err(|err| match err {
                    PathValidationError::InvalidLength => E::invalid_length(
                        s.len(),
                        &"a non-empty path no more than 1024 characters in length",
                    ),
                    PathValidationError::MalformedPath => E::invalid_value(
                        de::Unexpected::Str(s),
                        &"a path with no leading `/` and non-empty segments",
                    ),
                })
            }
        }
        deserializer.deserialize_string(Visitor)
    }
}

/// A component URL. The URL is validated, but represented as a string to avoid
/// normalization and retain the original representation.
#[derive(Serialize, Clone, Debug)]
pub struct Url(String);

/// The error representing a failed validation of a `Url` string.
#[derive(Debug, Error)]
pub enum UrlValidationError {
    /// The URL string is empty or greater than 4096 characters in length.
    #[error("url must be non-empty and no more than 4096 characters")]
    InvalidLength,
    /// The URL string is not a valid URL. See the [`Url::new`].
    #[error("url is malformed")]
    MalformedUrl,
}

impl Url {
    /// Creates a `Url` from a `String`, returning an `Err` if the string fails
    /// validation. The string must be non-empty, no more than 4096 characters
    /// in length, and be a valid URL. See the [`url`](../../url/index.html) crate.
    pub fn new(url: String) -> Result<Self, UrlValidationError> {
        Self::from_str_impl(Cow::Owned(url))
    }

    fn from_str_impl(url_str: Cow<'_, str>) -> Result<Self, UrlValidationError> {
        if url_str.is_empty() || url_str.len() > 4096 {
            return Err(UrlValidationError::InvalidLength);
        }
        let parsed_url = url::Url::parse(&url_str).map_err(|_| UrlValidationError::MalformedUrl)?;
        if parsed_url.cannot_be_a_base() {
            return Err(UrlValidationError::MalformedUrl);
        }
        // Use the unparsed URL string so that the original format is preserved.
        Ok(Self(url_str.into_owned()))
    }

    pub fn as_str(&self) -> &str {
        self.0.as_str()
    }
}

impl FromStr for Url {
    type Err = UrlValidationError;

    fn from_str(url: &str) -> Result<Self, Self::Err> {
        Self::from_str_impl(Cow::Borrowed(url))
    }
}

impl From<Url> for String {
    fn from(url: Url) -> String {
        url.0
    }
}

impl<'de> de::Deserialize<'de> for Url {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: de::Deserializer<'de>,
    {
        struct Visitor;

        impl<'de> de::Visitor<'de> for Visitor {
            type Value = Url;

            fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str("a non-empty URL no more than 4096 characters in length")
            }

            fn visit_str<E>(self, s: &str) -> Result<Self::Value, E>
            where
                E: de::Error,
            {
                s.parse().map_err(|err| match err {
                    UrlValidationError::InvalidLength => E::invalid_length(
                        s.len(),
                        &"a non-empty URL no more than 4096 characters in length",
                    ),
                    UrlValidationError::MalformedUrl => {
                        E::invalid_value(de::Unexpected::Str(s), &"a valid URL")
                    }
                })
            }
        }
        deserializer.deserialize_string(Visitor)
    }
}

/// A URL scheme.
#[derive(Clone, Debug, Eq, Hash, PartialEq, Serialize)]
pub struct UrlScheme(String);

/// The error representing a failed validation of a `UrlScheme` string.
#[derive(Debug, Error)]
pub enum UrlSchemeValidationError {
    /// The URL scheme is empty or greater than 100 characters in length.
    #[error("URL scheme must be non-empty and no more than 100 characters, got {} characters", 0)]
    InvalidLength(usize),
    /// The URL scheme is not valid. See [`UrlScheme::new`].
    #[error("Invalid character in URL scheme: '{}'", 0)]
    MalformedUrlScheme(char),
}

impl UrlScheme {
    /// Creates a `UrlScheme` from a `String`, returning an `Err` if the string fails
    /// validation. The string must be non-empty and no more than 100 characters
    /// in length. It must start with a lowercase ASCII letter (a-z),
    /// and contain only lowercase ASCII letters, digits, `+`, `-`, and `.`.
    pub fn new(url_scheme: String) -> Result<Self, UrlSchemeValidationError> {
        Self::validate(&url_scheme)?;
        Ok(UrlScheme(url_scheme))
    }

    /// Validates `url_scheme` but does not construct a new `UrlScheme` object.
    /// See [`UrlScheme::new`] for validation details.
    pub fn validate(url_scheme: &str) -> Result<(), UrlSchemeValidationError> {
        if url_scheme.is_empty() || url_scheme.len() > 100 {
            return Err(UrlSchemeValidationError::InvalidLength(url_scheme.len()));
        }
        let mut iter = url_scheme.chars();
        let first_char = iter.next().unwrap();
        if !first_char.is_ascii_lowercase() {
            return Err(UrlSchemeValidationError::MalformedUrlScheme(first_char));
        }
        if let Some(c) = iter.find(|&c| {
            !c.is_ascii_lowercase() && !c.is_ascii_digit() && c != '.' && c != '+' && c != '-'
        }) {
            return Err(UrlSchemeValidationError::MalformedUrlScheme(c));
        }
        Ok(())
    }

    pub fn as_str(&self) -> &str {
        self.0.as_str()
    }
}

impl fmt::Display for UrlScheme {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(&self.0, f)
    }
}

impl FromStr for UrlScheme {
    type Err = UrlSchemeValidationError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Self::validate(s)?;
        Ok(UrlScheme(s.to_string()))
    }
}

impl From<UrlScheme> for String {
    fn from(u: UrlScheme) -> String {
        u.0
    }
}

impl<'de> de::Deserialize<'de> for UrlScheme {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: de::Deserializer<'de>,
    {
        struct Visitor;

        impl<'de> de::Visitor<'de> for Visitor {
            type Value = UrlScheme;

            fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str("a non-empty URL scheme no more than 100 characters in length")
            }

            fn visit_str<E>(self, s: &str) -> Result<Self::Value, E>
            where
                E: de::Error,
            {
                s.parse().map_err(|err| match err {
                    UrlSchemeValidationError::InvalidLength(l) => E::invalid_length(
                        l,
                        &"a non-empty URL scheme no more than 100 characters in length",
                    ),
                    UrlSchemeValidationError::MalformedUrlScheme(_) => {
                        E::invalid_value(de::Unexpected::Str(s), &"a valid URL scheme")
                    }
                })
            }
        }
        deserializer.deserialize_string(Visitor)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        serde_json::{self, json},
        std::iter::repeat,
    };

    macro_rules! expect_ok {
        ($type_:ty, $($input:tt)+) => {
            assert!(serde_json::from_str::<$type_>(&json!($($input)*).to_string()).is_ok());
        };
    }

    macro_rules! expect_err {
        ($type_:ty, $($input:tt)+) => {
            assert!(serde_json::from_str::<$type_>(&json!($($input)*).to_string()).is_err());
        };
    }

    #[test]
    fn test_valid_name() {
        expect_ok!(Name, "foo");
        expect_ok!(Name, "foO123._-");
        expect_ok!(Name, repeat("x").take(100).collect::<String>());
    }

    #[test]
    fn test_invalid_name() {
        expect_err!(Name, "");
        expect_err!(Name, "@&%^");
        expect_err!(Name, repeat("x").take(101).collect::<String>());
    }

    #[test]
    fn test_valid_path() {
        expect_ok!(Path, "/foo");
        expect_ok!(Path, "/foo/bar");
        expect_ok!(Path, &format!("/{}", repeat("x").take(1023).collect::<String>()));
    }

    #[test]
    fn test_invalid_path() {
        expect_err!(Path, "");
        expect_err!(Path, "/");
        expect_err!(Path, "foo");
        expect_err!(Path, "foo/");
        expect_err!(Path, "/foo/");
        expect_err!(Path, "/foo//bar");
        expect_err!(Path, &format!("/{}", repeat("x").take(1024).collect::<String>()));
    }

    #[test]
    fn test_valid_relative_path() {
        expect_ok!(RelativePath, "foo");
        expect_ok!(RelativePath, "foo/bar");
        expect_ok!(RelativePath, &format!("{}", repeat("x").take(1024).collect::<String>()));
    }

    #[test]
    fn test_invalid_relative_path() {
        expect_err!(RelativePath, "");
        expect_err!(RelativePath, "/");
        expect_err!(RelativePath, "/foo");
        expect_err!(RelativePath, "foo/");
        expect_err!(RelativePath, "/foo/");
        expect_err!(RelativePath, "foo//bar");
        expect_err!(RelativePath, &format!("{}", repeat("x").take(1025).collect::<String>()));
    }

    #[test]
    fn test_valid_url() {
        expect_ok!(Url, "a://foo");
        expect_ok!(Url, &format!("a://{}", repeat("x").take(4092).collect::<String>()));
    }

    #[test]
    fn test_invalid_url() {
        expect_err!(Url, "");
        expect_err!(Url, "foo");
        expect_err!(Url, &format!("a://{}", repeat("x").take(4093).collect::<String>()));
    }

    #[test]
    fn test_valid_url_scheme() {
        expect_ok!(UrlScheme, "fuch.sia-pkg+0");
        expect_ok!(UrlScheme, &format!("{}", repeat("f").take(100).collect::<String>()));
    }

    #[test]
    fn test_invalid_url_scheme() {
        expect_err!(UrlScheme, "");
        expect_err!(UrlScheme, "0fuch.sia-pkg+0");
        expect_err!(UrlScheme, "fuchsia_pkg");
        expect_err!(UrlScheme, "FUCHSIA-PKG");
        expect_err!(UrlScheme, &format!("{}", repeat("f").take(101).collect::<String>()));
    }

    #[test]
    fn test_name_error_message() {
        let input = r#"
            "foo$"
        "#;
        let err = serde_json::from_str::<Name>(input).expect_err("must fail");
        assert_eq!(
            err.to_string(),
            "invalid value: string \"foo$\", expected a name containing only \
             alpha-numeric characters or [_-.] at line 2 column 18"
        );
        assert_eq!(err.line(), 2);
        assert_eq!(err.column(), 18);
    }

    #[test]
    fn test_path_error_message() {
        let input = r#"
            "foo";
        "#;
        let err = serde_json::from_str::<Path>(input).expect_err("must fail");
        assert_eq!(
            err.to_string(),
            "invalid value: string \"foo\", expected a path with leading `/` \
             and non-empty segments at line 2 column 17"
        );

        assert_eq!(err.line(), 2);
        assert_eq!(err.column(), 17);
    }

    #[test]
    fn test_url_error_message() {
        let input = r#"
            "foo";
        "#;
        let err = serde_json::from_str::<Url>(input).expect_err("must fail");
        assert_eq!(
            err.to_string(),
            "invalid value: string \"foo\", expected a valid URL at line 2 \
             column 17"
        );
        assert_eq!(err.line(), 2);
        assert_eq!(err.column(), 17);
    }

    #[test]
    fn test_url_scheme_error_message() {
        let input = r#"
            "9fuchsia_pkg"
        "#;
        let err = serde_json::from_str::<UrlScheme>(input).expect_err("must fail");
        assert_eq!(
            err.to_string(),
            "invalid value: string \"9fuchsia_pkg\", expected a valid URL scheme at line 2 column 26"
        );
        assert_eq!(err.line(), 2);
        assert_eq!(err.column(), 26);
    }
}
