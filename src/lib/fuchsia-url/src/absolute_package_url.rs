// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::ParseError,
        parse::{PackageName, PackageVariant},
        PinnedAbsolutePackageUrl, RepositoryUrl, UnpinnedAbsolutePackageUrl, UrlParts,
    },
    fuchsia_hash::Hash,
};

/// A URL locating a Fuchsia package.
/// Has the form "fuchsia-pkg://<repository>/<name>[/variant][?hash=<hash>]" where:
///   * "repository" is a valid hostname
///   * "name" is a valid package name
///   * "variant" is an optional valid package variant
///   * "hash" is an optional valid package hash
/// https://fuchsia.dev/fuchsia-src/concepts/packages/package_url
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum AbsolutePackageUrl {
    Unpinned(UnpinnedAbsolutePackageUrl),
    Pinned(PinnedAbsolutePackageUrl),
}

impl AbsolutePackageUrl {
    pub(crate) fn from_parts(parts: UrlParts) -> Result<Self, ParseError> {
        let UrlParts { scheme, host, path, hash, resource } = parts;
        let repo = RepositoryUrl::new(
            scheme.ok_or(ParseError::MissingScheme)?,
            host.ok_or(ParseError::MissingHost)?,
        )?;
        if resource.is_some() {
            return Err(ParseError::CannotContainResource);
        }
        Self::new_with_path(repo, &path, hash)
    }

    /// Parse a "fuchsia-pkg://" URL that locates an optionally pinned package.
    pub fn parse(url: &str) -> Result<Self, ParseError> {
        Self::from_parts(UrlParts::parse(url)?)
    }

    /// Create an AbsolutePackageUrl from its component parts and a &str `path` that will be
    /// validated.
    pub fn new_with_path(
        repo: RepositoryUrl,
        path: &str,
        hash: Option<Hash>,
    ) -> Result<Self, ParseError> {
        Ok(match hash {
            None => Self::Unpinned(UnpinnedAbsolutePackageUrl::new_with_path(repo, path)?),
            Some(hash) => Self::Pinned(PinnedAbsolutePackageUrl::new_with_path(repo, path, hash)?),
        })
    }

    /// Create an AbsolutePackageUrl from its component parts.
    pub fn new(
        repo: RepositoryUrl,
        name: PackageName,
        variant: Option<PackageVariant>,
        hash: Option<Hash>,
    ) -> Self {
        match hash {
            None => Self::Unpinned(UnpinnedAbsolutePackageUrl::new(repo, name, variant)),
            Some(hash) => Self::Pinned(PinnedAbsolutePackageUrl::new(repo, name, variant, hash)),
        }
    }

    /// The optional hash of the package.
    pub fn hash(&self) -> Option<Hash> {
        match self {
            Self::Unpinned(_) => None,
            Self::Pinned(pinned) => Some(pinned.hash()),
        }
    }

    /// The URL without the optional package hash.
    pub fn as_unpinned(&self) -> &UnpinnedAbsolutePackageUrl {
        match self {
            Self::Unpinned(unpinned) => &unpinned,
            Self::Pinned(pinned) => pinned.as_unpinned(),
        }
    }
}

// AbsolutePackageUrl does not maintain any invariants in addition to those already maintained by
// its variants so this is safe.
impl std::ops::Deref for AbsolutePackageUrl {
    type Target = UnpinnedAbsolutePackageUrl;

    fn deref(&self) -> &Self::Target {
        match self {
            Self::Unpinned(unpinned) => &unpinned,
            Self::Pinned(pinned) => &pinned,
        }
    }
}

// AbsolutePackageUrl does not maintain any invariants in addition to those already maintained by
// its variants so this is safe.
impl std::ops::DerefMut for AbsolutePackageUrl {
    fn deref_mut(&mut self) -> &mut Self::Target {
        match self {
            Self::Unpinned(unpinned) => unpinned,
            Self::Pinned(pinned) => pinned,
        }
    }
}

impl std::str::FromStr for AbsolutePackageUrl {
    type Err = ParseError;

    fn from_str(url: &str) -> Result<Self, Self::Err> {
        Self::parse(url)
    }
}

impl std::convert::TryFrom<&str> for AbsolutePackageUrl {
    type Error = ParseError;

    fn try_from(value: &str) -> Result<Self, Self::Error> {
        Self::parse(value)
    }
}

impl std::convert::From<PinnedAbsolutePackageUrl> for AbsolutePackageUrl {
    fn from(pinned: PinnedAbsolutePackageUrl) -> Self {
        Self::Pinned(pinned)
    }
}

impl std::convert::From<UnpinnedAbsolutePackageUrl> for AbsolutePackageUrl {
    fn from(unpinned: UnpinnedAbsolutePackageUrl) -> Self {
        Self::Unpinned(unpinned)
    }
}

impl std::fmt::Display for AbsolutePackageUrl {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Unpinned(unpinned) => write!(f, "{}", unpinned),
            Self::Pinned(pinned) => write!(f, "{}", pinned),
        }
    }
}

impl serde::Serialize for AbsolutePackageUrl {
    fn serialize<S: serde::Serializer>(&self, ser: S) -> Result<S::Ok, S::Error> {
        self.to_string().serialize(ser)
    }
}

impl<'de> serde::Deserialize<'de> for AbsolutePackageUrl {
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
            ("example.org/name", ParseError::MissingScheme),
            ("//example.org/name", ParseError::MissingScheme),
            ("///name", ParseError::MissingScheme),
            ("/name", ParseError::MissingScheme),
            ("name", ParseError::MissingScheme),
            ("fuchsia-boot://example.org/name", ParseError::InvalidScheme),
            ("fuchsia-pkg://", ParseError::MissingHost),
            ("fuchsia-pkg://exaMple.org", ParseError::InvalidHost),
            ("fuchsia-pkg://example.org/", ParseError::MissingName),
            (
                "fuchsia-pkg://example.org//",
                ParseError::InvalidPathSegment(PackagePathSegmentError::Empty),
            ),
            ("fuchsia-pkg://example.org/name/variant/extra", ParseError::ExtraPathSegments),
            ("fuchsia-pkg://example.org/name#resource", ParseError::CannotContainResource),
        ] {
            assert_matches!(
                AbsolutePackageUrl::parse(url),
                Err(e) if e == err,
                "the url {:?}", url
            );
            assert_matches!(
                url.parse::<AbsolutePackageUrl>(),
                Err(e) if e == err,
                "the url {:?}", url
            );
            assert_matches!(
                AbsolutePackageUrl::try_from(url),
                Err(e) if e == err,
                "the url {:?}", url
            );
            assert_matches!(
                serde_json::from_str::<AbsolutePackageUrl>(url),
                Err(_),
                "the url {:?}",
                url
            );
        }
    }

    #[test]
    fn parse_ok() {
        for (url, host, name, variant, hash) in [
            ("fuchsia-pkg://example.org/name", "example.org", "name", None, None),
            ("fuchsia-pkg://example.org/name/variant", "example.org", "name", Some("variant"), None),
            (
            "fuchsia-pkg://example.org/name?hash=0000000000000000000000000000000000000000000000000000000000000000", "example.org", "name", None, Some("0000000000000000000000000000000000000000000000000000000000000000")),
            ("fuchsia-pkg://example.org/name/variant?hash=0000000000000000000000000000000000000000000000000000000000000000", "example.org", "name", Some("variant"), Some("0000000000000000000000000000000000000000000000000000000000000000")),
        ] {
            let json_url = format!("\"{url}\"");

            // Creation
            let name = name.parse::<crate::PackageName>().unwrap();
            let variant = variant.map(|v| v.parse::<crate::PackageVariant>().unwrap());
            let hash = hash.map(|h| h.parse::<Hash>().unwrap());
            let validate = |parsed: &AbsolutePackageUrl| {
                assert_eq!(parsed.host(), host);
                assert_eq!(parsed.name(), &name);
                assert_eq!(parsed.variant(), variant.as_ref());
                assert_eq!(parsed.hash(), hash);
            };
            validate(&AbsolutePackageUrl::parse(url).unwrap());
            validate(&url.parse::<AbsolutePackageUrl>().unwrap());
            validate(&AbsolutePackageUrl::try_from(url).unwrap());
            validate(&serde_json::from_str::<AbsolutePackageUrl>(&json_url).unwrap());

            // Stringification
            assert_eq!(
                AbsolutePackageUrl::parse(url).unwrap().to_string(),
                url,
                "the url {:?}",
                url
            );
            assert_eq!(
                serde_json::to_string(&AbsolutePackageUrl::parse(url).unwrap()).unwrap(),
                json_url,
                "the url {:?}",
                url
            );
        }
    }
}
