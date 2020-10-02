// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A crate containing common Component Manager types used in Component Manifests
//! (`.cml` files and binary `.cm` files). These types come with `serde` serialization
//! and deserialization implementations that perform the required validation.

use {
    serde::{de, ser},
    serde::{Deserialize, Serialize},
    std::{borrow::Cow, default::Default, fmt, str::FromStr},
    thiserror::Error,
    url,
};

/// A name that can refer to a component, collection, or other entity in the
/// Component Manifest.
#[derive(Serialize, Clone, Debug, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub struct Name(String);

/// The error representing a failure to parse a type from string.
#[derive(Debug, Error)]
pub enum ParseError {
    /// The string did not match a valid value.
    #[error("invalid value")]
    InvalidValue,
    /// The string was too long or too short.
    #[error("invalid length")]
    InvalidLength,
    /// A name was expected and the string was a path.
    #[error("not a name")]
    NotAName,
    /// A path was expected and the string was a name.
    #[error("not a path")]
    NotAPath,
}

impl Name {
    /// Creates a `Name` from a `String`, returning an `Err` if the string
    /// fails validation. The string must be non-empty, no more than 100
    /// characters in length, and consist of one or more of the
    /// following characters: `a-z`, `0-9`, `_`, `.`, `-`.
    pub fn new(name: String) -> Result<Self, ParseError> {
        Self::from_str_impl(Cow::Owned(name))
    }

    fn from_str_impl(name: Cow<'_, str>) -> Result<Self, ParseError> {
        if name.is_empty() || name.len() > 100 {
            return Err(ParseError::InvalidLength);
        }
        let valid_fn = |c: char| c.is_ascii_alphanumeric() || c == '_' || c == '-' || c == '.';
        if !name.chars().all(valid_fn) {
            return Err(ParseError::InvalidValue);
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
    type Err = ParseError;

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
                    ParseError::InvalidValue => E::invalid_value(
                        de::Unexpected::Str(s),
                        &"a name containing only alpha-numeric characters or [_-.]",
                    ),
                    ParseError::InvalidLength => E::invalid_length(
                        s.len(),
                        &"a non-empty name no more than 100 characters in length",
                    ),
                    e => {
                        panic!("unexpected parse error: {:?}", e);
                    }
                })
            }
        }
        deserializer.deserialize_string(Visitor)
    }
}

/// A filesystem path.
#[derive(Serialize, Clone, Debug, PartialEq, Eq, Hash)]
pub struct Path(String);

impl Path {
    /// Creates a `Path` from a `String`, returning an `Err` if the string
    /// fails validation. The string must be non-empty, no more than 1024
    /// characters in length, start with a leading `/`, and contain no empty
    /// path segments.
    pub fn new(path: String) -> Result<Self, ParseError> {
        Self::validate(&path)?;
        Ok(Path(path))
    }

    /// Validates `path` but does not construct a new `Path` object.
    pub fn validate(path: &str) -> Result<(), ParseError> {
        if path.is_empty() || path.len() > 1024 {
            return Err(ParseError::InvalidLength);
        }
        if !path.starts_with('/') {
            return Err(ParseError::InvalidValue);
        }
        if !path[1..].split('/').all(|part| !part.is_empty()) {
            return Err(ParseError::InvalidValue);
        }
        Ok(())
    }

    pub fn as_str(&self) -> &str {
        self.0.as_str()
    }
}

impl FromStr for Path {
    type Err = ParseError;

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
                    ParseError::InvalidValue => E::invalid_value(
                        de::Unexpected::Str(s),
                        &"a path with leading `/` and non-empty segments",
                    ),
                    ParseError::InvalidLength => E::invalid_length(
                        s.len(),
                        &"a non-empty path no more than 1024 characters in length",
                    ),
                    e => {
                        panic!("unexpected parse error: {:?}", e);
                    }
                })
            }
        }
        deserializer.deserialize_string(Visitor)
    }
}

/// A string that could be a name or filesystem path.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub enum NameOrPath {
    Name(Name),
    Path(Path),
}

impl NameOrPath {
    /// Get the name if this is a name, or return an error if not.
    pub fn extract_name(self) -> Result<Name, ParseError> {
        match self {
            Self::Name(n) => Ok(n),
            Self::Path(_) => Err(ParseError::NotAName),
        }
    }

    /// Get the name if this is a name, or return an error if not.
    pub fn extract_name_borrowed(&self) -> Result<&Name, ParseError> {
        match self {
            Self::Name(ref n) => Ok(n),
            Self::Path(_) => Err(ParseError::NotAName),
        }
    }

    /// Get the path if this is a path, or return an error if not.
    pub fn extract_path(self) -> Result<Path, ParseError> {
        match self {
            Self::Path(p) => Ok(p),
            Self::Name(_) => Err(ParseError::NotAPath),
        }
    }

    /// Get the path if this is a path, or return an error if not.
    pub fn extract_path_borrowed(&self) -> Result<&Path, ParseError> {
        match self {
            Self::Path(ref p) => Ok(p),
            Self::Name(_) => Err(ParseError::NotAPath),
        }
    }

    /// Return true if this object holds a name.
    pub fn is_name(&self) -> bool {
        match self {
            Self::Name(_) => true,
            _ => false,
        }
    }

    /// Return true if this object holds a path.
    pub fn is_path(&self) -> bool {
        match self {
            Self::Path(_) => true,
            _ => false,
        }
    }

    /// Return a string representation of this name or path.
    pub fn as_str(&self) -> &str {
        match self {
            Self::Name(s) => &s.0,
            Self::Path(s) => &s.0,
        }
    }
}

impl FromStr for NameOrPath {
    type Err = ParseError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Ok(if s.starts_with("/") {
            NameOrPath::Path(s.parse()?)
        } else {
            NameOrPath::Name(s.parse()?)
        })
    }
}

impl From<NameOrPath> for String {
    fn from(n: NameOrPath) -> Self {
        n.to_string()
    }
}

impl fmt::Display for NameOrPath {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self.as_str(), f)
    }
}

impl ser::Serialize for NameOrPath {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: ser::Serializer,
    {
        serializer.serialize_str(&self.to_string())
    }
}

impl<'de> de::Deserialize<'de> for NameOrPath {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: de::Deserializer<'de>,
    {
        struct Visitor;

        impl<'de> de::Visitor<'de> for Visitor {
            type Value = NameOrPath;

            fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str(
                    "a non-empty path no more than 1024 characters \
                     in length, with a leading `/`, and containing no \
                     empty path segments, or \
                     a non-empty string no more than 100 characters in \
                     length, containing only alpha-numeric characters",
                )
            }

            fn visit_str<E>(self, s: &str) -> Result<Self::Value, E>
            where
                E: de::Error,
            {
                s.parse().map_err(|err| match err {
                    ParseError::InvalidValue => E::invalid_value(
                        de::Unexpected::Str(s),
                        &"a path with leading `/` and non-empty segments, or \
                              a name containing only alpha-numeric characters or [_-.]",
                    ),
                    ParseError::InvalidLength => E::invalid_length(
                        s.len(),
                        &"a non-empty path no more than 1024 characters in length, or \
                              a non-empty name no more than 100 characters in length",
                    ),
                    e => {
                        panic!("unexpected parse error: {:?}", e);
                    }
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
    pub fn new(path: String) -> Result<Self, ParseError> {
        Self::from_str_impl(Cow::Owned(path))
    }

    fn from_str_impl(path: Cow<'_, str>) -> Result<Self, ParseError> {
        if path.is_empty() || path.len() > 1024 {
            return Err(ParseError::InvalidLength);
        }
        if !path.split('/').all(|part| !part.is_empty()) {
            return Err(ParseError::InvalidValue);
        }
        return Ok(Self(path.into_owned()));
    }

    pub fn as_str(&self) -> &str {
        self.0.as_str()
    }
}

impl FromStr for RelativePath {
    type Err = ParseError;

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
                    ParseError::InvalidValue => E::invalid_value(
                        de::Unexpected::Str(s),
                        &"a path with no leading `/` and non-empty segments",
                    ),
                    ParseError::InvalidLength => E::invalid_length(
                        s.len(),
                        &"a non-empty path no more than 1024 characters in length",
                    ),
                    e => {
                        panic!("unexpected parse error: {:?}", e);
                    }
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

impl Url {
    /// Creates a `Url` from a `String`, returning an `Err` if the string fails
    /// validation. The string must be non-empty, no more than 4096 characters
    /// in length, and be a valid URL. See the [`url`](../../url/index.html) crate.
    pub fn new(url: String) -> Result<Self, ParseError> {
        Self::from_str_impl(Cow::Owned(url))
    }

    fn from_str_impl(url_str: Cow<'_, str>) -> Result<Self, ParseError> {
        if url_str.is_empty() || url_str.len() > 4096 {
            return Err(ParseError::InvalidLength);
        }
        let parsed_url = url::Url::parse(&url_str).map_err(|_| ParseError::InvalidValue)?;
        if parsed_url.cannot_be_a_base() {
            return Err(ParseError::InvalidValue);
        }
        // Use the unparsed URL string so that the original format is preserved.
        Ok(Self(url_str.into_owned()))
    }

    pub fn as_str(&self) -> &str {
        self.0.as_str()
    }
}

impl FromStr for Url {
    type Err = ParseError;

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
                    ParseError::InvalidValue => {
                        E::invalid_value(de::Unexpected::Str(s), &"a valid URL")
                    }
                    ParseError::InvalidLength => E::invalid_length(
                        s.len(),
                        &"a non-empty URL no more than 4096 characters in length",
                    ),
                    e => {
                        panic!("unexpected parse error: {:?}", e);
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

impl UrlScheme {
    /// Creates a `UrlScheme` from a `String`, returning an `Err` if the string fails
    /// validation. The string must be non-empty and no more than 100 characters
    /// in length. It must start with a lowercase ASCII letter (a-z),
    /// and contain only lowercase ASCII letters, digits, `+`, `-`, and `.`.
    pub fn new(url_scheme: String) -> Result<Self, ParseError> {
        Self::validate(&url_scheme)?;
        Ok(UrlScheme(url_scheme))
    }

    /// Validates `url_scheme` but does not construct a new `UrlScheme` object.
    /// See [`UrlScheme::new`] for validation details.
    pub fn validate(url_scheme: &str) -> Result<(), ParseError> {
        if url_scheme.is_empty() || url_scheme.len() > 100 {
            return Err(ParseError::InvalidLength);
        }
        let mut iter = url_scheme.chars();
        let first_char = iter.next().unwrap();
        if !first_char.is_ascii_lowercase() {
            return Err(ParseError::InvalidValue);
        }
        if let Some(_) = iter.find(|&c| {
            !c.is_ascii_lowercase() && !c.is_ascii_digit() && c != '.' && c != '+' && c != '-'
        }) {
            return Err(ParseError::InvalidValue);
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
    type Err = ParseError;

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
                    ParseError::InvalidValue => {
                        E::invalid_value(de::Unexpected::Str(s), &"a valid URL scheme")
                    }
                    ParseError::InvalidLength => E::invalid_length(
                        s.len(),
                        &"a non-empty URL scheme no more than 100 characters in length",
                    ),
                    e => {
                        panic!("unexpected parse error: {:?}", e);
                    }
                })
            }
        }
        deserializer.deserialize_string(Visitor)
    }
}

/// The duration of child components in a collection. See [`Durability`].
///
/// [`Durability`]: ../../fidl_fuchsia_sys2/enum.Durability.html
#[derive(Serialize, Deserialize, Clone, Debug)]
#[serde(rename_all = "snake_case")]
pub enum Durability {
    /// The instance exists until it is explicitly destroyed.
    Persistent,
    /// The instance exists until its containing realm is stopped or it is
    /// explicitly destroyed.
    Transient,
}

/// A component instance's startup mode. See [`StartupMode`].
///
/// [`StartupMode`]: ../../fidl_fuchsia_sys2/enum.StartupMode.html
#[derive(Debug, Serialize, Deserialize, Clone)]
#[serde(rename_all = "snake_case")]
pub enum StartupMode {
    /// Start the component instance only if another component instance binds to
    /// it.
    Lazy,
    /// Start the component instance as soon as its parent starts.
    Eager,
}

impl Default for StartupMode {
    fn default() -> Self {
        Self::Lazy
    }
}

/// Offered dependency type. See [`DependencyType`].
///
/// [`DependencyType`]: ../../fidl_fuchsia_sys2/enum.DependencyType.html
#[derive(Debug, Serialize, Deserialize, Clone, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum DependencyType {
    Strong,
    WeakForMigration,
}

impl Default for DependencyType {
    fn default() -> Self {
        Self::Strong
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
