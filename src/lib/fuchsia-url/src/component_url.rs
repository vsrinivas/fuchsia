// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{errors::ParseError, AbsoluteComponentUrl, PackageUrl, RelativeComponentUrl, UrlParts};

/// A URL locating a Fuchsia component. Can be either absolute or relative.
/// See `AbsoluteComponentUrl` and `RelativeComponentUrl` for more details.
/// https://fuchsia.dev/fuchsia-src/concepts/packages/package_url
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ComponentUrl {
    package: PackageUrl,
    resource: String,
}

impl ComponentUrl {
    /// Parse a Component URL.
    pub fn parse(url: &str) -> Result<Self, ParseError> {
        let parts = UrlParts::parse(url)?;
        Ok(if parts.scheme.is_some() {
            let (absolute, resource) =
                AbsoluteComponentUrl::from_parts(parts)?.into_package_and_resource();
            Self { package: absolute.into(), resource }
        } else {
            let (relative, resource) =
                RelativeComponentUrl::from_parts(parts)?.into_package_and_resource();
            Self { package: relative.into(), resource }
        })
    }

    /// The package URL of this URL (this URL without the resource path).
    pub fn package_url(&self) -> &PackageUrl {
        &self.package
    }

    /// The resource path of this URL.
    pub fn resource(&self) -> &str {
        &self.resource
    }
}

impl std::str::FromStr for ComponentUrl {
    type Err = ParseError;

    fn from_str(url: &str) -> Result<Self, Self::Err> {
        Self::parse(url)
    }
}

impl std::convert::TryFrom<&str> for ComponentUrl {
    type Error = ParseError;

    fn try_from(value: &str) -> Result<Self, Self::Error> {
        Self::parse(value)
    }
}

impl std::fmt::Display for ComponentUrl {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "{}#{}",
            self.package,
            percent_encoding::utf8_percent_encode(&self.resource, crate::FRAGMENT)
        )
    }
}

impl serde::Serialize for ComponentUrl {
    fn serialize<S: serde::Serializer>(&self, ser: S) -> Result<S::Ok, S::Error> {
        self.to_string().serialize(ser)
    }
}

impl<'de> serde::Deserialize<'de> for ComponentUrl {
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
            "fuchsia-boot://example.org/name#resource",
            "fuchsia-pkg://name#resource",
            "fuchsia-pkg://example.org#resource",
            "fuchsia-pkg://example.org/#resource",
            "fuchsia-pkg://example.org//#resource",
            "fuchsia-pkg://example.org/name",
            "fuchsia-pkg://exaMple.org/name#resource",
            "fuchsia-pkg:///name#resource",
            "fuchsia-pkg://#resource",
            "example.org/name#resource",
            "name/variant#resource",
            "name",
            "name?hash=0000000000000000000000000000000000000000000000000000000000000000",
            "#resource",
        ] {
            assert_matches!(ComponentUrl::parse(url), Err(_), "the url {:?}", url);
            assert_matches!(url.parse::<ComponentUrl>(), Err(_), "the url {:?}", url);
            assert_matches!(ComponentUrl::try_from(url), Err(_), "the url {:?}", url);
            assert_matches!(serde_json::from_str::<ComponentUrl>(url), Err(_), "the url {:?}", url);
        }
    }

    #[test]
    fn parse_ok_absolute() {
        for url in [
            "fuchsia-pkg://example.org/name#resource",
            "fuchsia-pkg://example.org/name#resource%09",
            "fuchsia-pkg://example.org/name/variant#resource",
            "fuchsia-pkg://example.org/name?hash=0000000000000000000000000000000000000000000000000000000000000000#resource",
            "fuchsia-pkg://example.org/name/variant?hash=0000000000000000000000000000000000000000000000000000000000000000#resource",
        ] {
            let json_url = format!("\"{url}\"");
            let validate = |parsed: &ComponentUrl| {
                assert_eq!(parsed.to_string(), url);
                assert_eq!(serde_json::to_string(&parsed).unwrap(), json_url);
            };
            validate(&ComponentUrl::parse(url).unwrap());
            validate(&url.parse::<ComponentUrl>().unwrap());
            validate(&ComponentUrl::try_from(url).unwrap());
            validate(&serde_json::from_str::<ComponentUrl>(&json_url).unwrap());
        }
    }

    #[test]
    fn parse_ok_relative() {
        for url in ["name#resource", "other-name#resource%09"] {
            let json_url = format!("\"{url}\"");
            let validate = |parsed: &ComponentUrl| {
                assert_eq!(parsed.to_string(), url);
                assert_eq!(serde_json::to_string(&parsed).unwrap(), json_url);
            };
            validate(&ComponentUrl::parse(url).unwrap());
            validate(&url.parse::<ComponentUrl>().unwrap());
            validate(&ComponentUrl::try_from(url).unwrap());
            validate(&serde_json::from_str::<ComponentUrl>(&json_url).unwrap());
        }
    }
}
