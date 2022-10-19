// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{errors::ParseError, AbsolutePackageUrl, RelativePackageUrl, UrlParts};

/// A URL locating a Fuchsia package. Can be either absolute or relative.
/// See `AbsolutePackageUrl` and `RelativePackageUrl` for more details.
/// https://fuchsia.dev/fuchsia-src/concepts/packages/package_url
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum PackageUrl {
    Absolute(AbsolutePackageUrl),
    Relative(RelativePackageUrl),
}

impl PackageUrl {
    /// Parse a package URL.
    pub fn parse(url: &str) -> Result<Self, ParseError> {
        let parts = UrlParts::parse(url)?;
        Ok(if parts.scheme.is_some() {
            Self::Absolute(AbsolutePackageUrl::from_parts(parts)?)
        } else {
            Self::Relative(RelativePackageUrl::from_parts(parts)?)
        })
    }
}

impl std::str::FromStr for PackageUrl {
    type Err = ParseError;

    fn from_str(url: &str) -> Result<Self, Self::Err> {
        Self::parse(url)
    }
}

impl std::convert::TryFrom<&str> for PackageUrl {
    type Error = ParseError;

    fn try_from(value: &str) -> Result<Self, Self::Error> {
        Self::parse(value)
    }
}

impl From<AbsolutePackageUrl> for PackageUrl {
    fn from(absolute: AbsolutePackageUrl) -> Self {
        Self::Absolute(absolute)
    }
}

impl From<RelativePackageUrl> for PackageUrl {
    fn from(relative: RelativePackageUrl) -> Self {
        Self::Relative(relative)
    }
}

impl std::fmt::Display for PackageUrl {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Absolute(absolute) => absolute.fmt(f),
            Self::Relative(relative) => relative.fmt(f),
        }
    }
}

impl serde::Serialize for PackageUrl {
    fn serialize<S: serde::Serializer>(&self, ser: S) -> Result<S::Ok, S::Error> {
        self.to_string().serialize(ser)
    }
}

impl<'de> serde::Deserialize<'de> for PackageUrl {
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
    use {super::*, assert_matches::assert_matches, std::convert::TryFrom as _};

    #[test]
    fn parse_err() {
        for url in [
            "fuchsia-boot://example.org/name",
            "fuchsia-pkg://",
            "fuchsia-pkg://example.org/",
            "fuchsia-pkg://example.org//",
            "fuchsia-pkg://exaMple.org/name",
            "fuchsia-pkg:///name",
            "fuchsia-pkg://name",
            "example.org/name",
            "name/variant",
            "name#resource",
            "name?hash=0000000000000000000000000000000000000000000000000000000000000000",
        ] {
            assert_matches!(PackageUrl::parse(url), Err(_), "the url {:?}", url);
            assert_matches!(url.parse::<PackageUrl>(), Err(_), "the url {:?}", url);
            assert_matches!(PackageUrl::try_from(url), Err(_), "the url {:?}", url);
            assert_matches!(serde_json::from_str::<PackageUrl>(url), Err(_), "the url {:?}", url);
        }
    }

    #[test]
    fn parse_ok_absolute() {
        for url in [
            "fuchsia-pkg://example.org/name",
            "fuchsia-pkg://example.org/name/variant",
            "fuchsia-pkg://example.org/name?hash=0000000000000000000000000000000000000000000000000000000000000000",
            "fuchsia-pkg://example.org/name/variant?hash=0000000000000000000000000000000000000000000000000000000000000000",
        ] {
            let json_url = format!("\"{url}\"");
            let validate = |parsed: &PackageUrl| {
                assert_eq!(parsed.to_string(), url);
                assert_eq!(serde_json::to_string(&parsed).unwrap(), json_url);
            };
            validate(&PackageUrl::parse(url).unwrap());
            validate(&url.parse::<PackageUrl>().unwrap());
            validate(&PackageUrl::try_from(url).unwrap());
            validate(&serde_json::from_str::<PackageUrl>(&json_url).unwrap());
        }
    }

    #[test]
    fn parse_ok_relative() {
        for url in ["name", "other3-name"] {
            let json_url = format!("\"{url}\"");
            let validate = |parsed: &PackageUrl| {
                assert_eq!(parsed.to_string(), url);
                assert_eq!(serde_json::to_string(&parsed).unwrap(), json_url);
            };
            validate(&PackageUrl::parse(url).unwrap());
            validate(&url.parse::<PackageUrl>().unwrap());
            validate(&PackageUrl::try_from(url).unwrap());
            validate(&serde_json::from_str::<PackageUrl>(&json_url).unwrap());
        }
    }
}
