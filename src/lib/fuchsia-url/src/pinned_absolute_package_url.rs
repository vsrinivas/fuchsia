// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::ParseError,
        parse::{PackageName, PackageVariant},
        AbsolutePackageUrl, RepositoryUrl, UnpinnedAbsolutePackageUrl,
    },
    fuchsia_hash::Hash,
};

/// A URL locating a Fuchsia package. Must have a hash.
/// Has the form "fuchsia-pkg://<repository>/<name>[/variant]?hash=<hash>" where:
///   * "repository" is a valid hostname
///   * "name" is a valid package name
///   * "variant" is an optional valid package variant
///   * "hash" is a valid package hash
/// https://fuchsia.dev/fuchsia-src/concepts/packages/package_url
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct PinnedAbsolutePackageUrl {
    unpinned: UnpinnedAbsolutePackageUrl,
    hash: Hash,
}

impl PinnedAbsolutePackageUrl {
    /// Create a `PinnedAbsolutePackageUrl` from its component parts.
    pub fn new(
        repo: RepositoryUrl,
        name: PackageName,
        variant: Option<PackageVariant>,
        hash: Hash,
    ) -> Self {
        Self { unpinned: UnpinnedAbsolutePackageUrl::new(repo, name, variant), hash }
    }

    /// Create a PinnedAbsolutePackageUrl from its component parts and a &str `path` that will be
    /// validated.
    pub fn new_with_path(repo: RepositoryUrl, path: &str, hash: Hash) -> Result<Self, ParseError> {
        Ok(Self { unpinned: UnpinnedAbsolutePackageUrl::new_with_path(repo, path)?, hash })
    }

    /// Parse a "fuchsia-pkg://" URL that locates a pinned (has a hash query parameter) package.
    pub fn parse(url: &str) -> Result<Self, ParseError> {
        match AbsolutePackageUrl::parse(url)? {
            AbsolutePackageUrl::Unpinned(_) => Err(ParseError::MissingHash),
            AbsolutePackageUrl::Pinned(pinned) => Ok(pinned),
        }
    }

    /// Create a `PinnedAbsolutePackageUrl` from an unpinned url and a hash.
    pub fn from_unpinned(unpinned: UnpinnedAbsolutePackageUrl, hash: Hash) -> Self {
        Self { unpinned, hash }
    }

    /// Split this URL into an unpinned URL and hash.
    pub fn into_unpinned_and_hash(self) -> (UnpinnedAbsolutePackageUrl, Hash) {
        let Self { unpinned, hash } = self;
        (unpinned, hash)
    }

    /// The URL without the hash.
    pub fn as_unpinned(&self) -> &UnpinnedAbsolutePackageUrl {
        &self.unpinned
    }

    /// The URL's hash.
    pub fn hash(&self) -> Hash {
        self.hash
    }
}

// PinnedAbsolutePackageUrl does not maintain any invariants on its `unpinned` field in addition to
// those already maintained by UnpinnedAbsolutePackageUrl so this is safe.
impl std::ops::Deref for PinnedAbsolutePackageUrl {
    type Target = UnpinnedAbsolutePackageUrl;

    fn deref(&self) -> &Self::Target {
        &self.unpinned
    }
}

// PinnedAbsolutePackageUrl does not maintain any invariants on its `unpinned` field in addition to
// those already maintained by UnpinnedAbsolutePackageUrl so this is safe.
impl std::ops::DerefMut for PinnedAbsolutePackageUrl {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.unpinned
    }
}

impl std::str::FromStr for PinnedAbsolutePackageUrl {
    type Err = ParseError;

    fn from_str(url: &str) -> Result<Self, Self::Err> {
        Self::parse(url)
    }
}

impl std::convert::TryFrom<&str> for PinnedAbsolutePackageUrl {
    type Error = ParseError;

    fn try_from(value: &str) -> Result<Self, Self::Error> {
        Self::parse(value)
    }
}

impl std::fmt::Display for PinnedAbsolutePackageUrl {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}?hash={}", self.unpinned, self.hash)
    }
}

impl serde::Serialize for PinnedAbsolutePackageUrl {
    fn serialize<S: serde::Serializer>(&self, ser: S) -> Result<S::Ok, S::Error> {
        self.to_string().serialize(ser)
    }
}

impl<'de> serde::Deserialize<'de> for PinnedAbsolutePackageUrl {
    fn deserialize<D>(de: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        let url = String::deserialize(de)?;
        Ok(Self::parse(&url).map_err(|err| serde::de::Error::custom(err))?)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::errors::PackagePathSegmentError, assert_matches::assert_matches,
        std::convert::TryFrom as _,
    };

    #[test]
    fn parse_err() {
        for (url, err) in [
            ("fuchsia-boot://example.org/name?hash=0000000000000000000000000000000000000000000000000000000000000000", ParseError::InvalidScheme),
            ("fuchsia-pkg://?hash=0000000000000000000000000000000000000000000000000000000000000000", ParseError::MissingHost),
            ("fuchsia-pkg://exaMple.org?hash=0000000000000000000000000000000000000000000000000000000000000000", ParseError::InvalidHost),
            ("fuchsia-pkg://example.org/?hash=0000000000000000000000000000000000000000000000000000000000000000", ParseError::MissingName),
            (
                "fuchsia-pkg://example.org//?hash=0000000000000000000000000000000000000000000000000000000000000000",
                ParseError::InvalidPathSegment(PackagePathSegmentError::Empty),
            ),
            ("fuchsia-pkg://example.org/name/variant/extra?hash=0000000000000000000000000000000000000000000000000000000000000000", ParseError::ExtraPathSegments),
            ("fuchsia-pkg://example.org/name?hash=0000000000000000000000000000000000000000000000000000000000000000#resource", ParseError::CannotContainResource),

        ] {
            assert_matches!(
                PinnedAbsolutePackageUrl::parse(url),
                Err(e) if e == err,
                "the url {:?}", url
            );
            assert_matches!(
                url.parse::<PinnedAbsolutePackageUrl>(),
                Err(e) if e == err,
                "the url {:?}", url
            );
            assert_matches!(
                PinnedAbsolutePackageUrl::try_from(url),
                Err(e) if e == err,
                "the url {:?}", url
            );
            assert_matches!(
                serde_json::from_str::<PinnedAbsolutePackageUrl>(url),
                Err(_),
                "the url {:?}",
                url
            );
        }
    }

    #[test]
    fn parse_ok() {
        for (url, variant, path) in [
            ("fuchsia-pkg://example.org/name?hash=0000000000000000000000000000000000000000000000000000000000000000", None, "/name"),
            (
                "fuchsia-pkg://example.org/name/variant?hash=0000000000000000000000000000000000000000000000000000000000000000",
                Some("variant"),
                "/name/variant",
            ),
        ] {
            let json_url = format!("\"{url}\"");
            let host = "example.org";
            let name = "name";
            let hash = "0000000000000000000000000000000000000000000000000000000000000000".parse::<Hash>().unwrap();

            // Creation
            let name = name.parse::<crate::PackageName>().unwrap();
            let variant = variant.map(|v| v.parse::<crate::PackageVariant>().unwrap());
            let validate = |parsed: &PinnedAbsolutePackageUrl| {
                assert_eq!(parsed.host(), host);
                assert_eq!(parsed.name(), &name);
                assert_eq!(parsed.variant(), variant.as_ref());
                assert_eq!(parsed.path(), path);
                assert_eq!(parsed.hash(), hash);
            };
            validate(&PinnedAbsolutePackageUrl::parse(url).unwrap());
            validate(&url.parse::<PinnedAbsolutePackageUrl>().unwrap());
            validate(&PinnedAbsolutePackageUrl::try_from(url).unwrap());
            validate(&serde_json::from_str::<PinnedAbsolutePackageUrl>(&json_url).unwrap());

            // Stringification
            assert_eq!(
                PinnedAbsolutePackageUrl::parse(url).unwrap().to_string(),
                url,
                "the url {:?}",
                url
            );
            assert_eq!(
                serde_json::to_string(&PinnedAbsolutePackageUrl::parse(url).unwrap()).unwrap(),
                json_url,
                "the url {:?}",
                url
            );
        }
    }
}
