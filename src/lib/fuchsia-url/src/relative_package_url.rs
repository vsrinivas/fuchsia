// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{errors::ParseError, parse::PackageName, UrlParts};

/// A relative URL locating a Fuchsia package. Used with a subpackage context.
/// Has the form "<name>" where:
///   * "name" is a valid package name
/// https://fuchsia.dev/fuchsia-src/concepts/packages/package_url
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct RelativePackageUrl {
    path: PackageName,
}

impl RelativePackageUrl {
    pub(crate) fn from_parts(parts: UrlParts) -> Result<Self, ParseError> {
        let UrlParts { scheme, host, path, hash, resource } = parts;
        if scheme.is_some() {
            return Err(ParseError::CannotContainScheme);
        }
        if host.is_some() {
            return Err(ParseError::HostMustBeEmpty);
        }
        if hash.is_some() {
            return Err(ParseError::CannotContainHash);
        }
        if resource.is_some() {
            return Err(ParseError::CannotContainResource);
        }
        let (name, variant) = crate::parse_path_to_name_and_variant(&path)?;
        if variant.is_some() {
            return Err(ParseError::RelativePathCannotSpecifyVariant);
        }
        Ok(Self { path: name })
    }

    /// Parse a relative package URL.
    pub fn parse(url: &str) -> Result<Self, ParseError> {
        let parts = UrlParts::parse(url)?;
        Ok(Self::from_parts(parts)?)
    }
}

impl std::str::FromStr for RelativePackageUrl {
    type Err = ParseError;

    fn from_str(url: &str) -> Result<Self, Self::Err> {
        Self::parse(url)
    }
}

impl From<PackageName> for RelativePackageUrl {
    fn from(path: PackageName) -> Self {
        Self { path }
    }
}

impl std::convert::TryFrom<&str> for RelativePackageUrl {
    type Error = ParseError;

    fn try_from(value: &str) -> Result<Self, Self::Error> {
        Self::parse(value)
    }
}

impl std::fmt::Display for RelativePackageUrl {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.path.fmt(f)
    }
}

impl AsRef<str> for RelativePackageUrl {
    fn as_ref(&self) -> &str {
        self.path.as_ref()
    }
}

impl From<&RelativePackageUrl> for String {
    fn from(url: &RelativePackageUrl) -> Self {
        url.to_string()
    }
}

impl serde::Serialize for RelativePackageUrl {
    fn serialize<S: serde::Serializer>(&self, ser: S) -> Result<S::Ok, S::Error> {
        self.to_string().serialize(ser)
    }
}

impl<'de> serde::Deserialize<'de> for RelativePackageUrl {
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
        for (url, err) in [
            ("fuchsia-boot://example.org/name", ParseError::CannotContainScheme),
            ("fuchsia-pkg://", ParseError::CannotContainScheme),
            ("fuchsia-pkg://name", ParseError::CannotContainScheme),
            ("fuchsia-pkg:///name", ParseError::CannotContainScheme),
            ("//example.org/name", ParseError::HostMustBeEmpty),
            ("///name", ParseError::HostMustBeEmpty),
            ("example.org/name", ParseError::RelativePathCannotSpecifyVariant),
            ("fuchsia-pkg://example.org/name", ParseError::CannotContainScheme),
            ("name/variant", ParseError::RelativePathCannotSpecifyVariant),
            ("name#resource", ParseError::CannotContainResource),
            (".", ParseError::MissingName),
            ("..", ParseError::MissingName),
            (
                "name?hash=0000000000000000000000000000000000000000000000000000000000000000",
                ParseError::CannotContainHash,
            ),
        ] {
            assert_matches!(
                RelativePackageUrl::parse(url),
                Err(e) if e == err,
                "the url {:?}", url
            );
            assert_matches!(
                url.parse::<RelativePackageUrl>(),
                Err(e) if e == err,
                "the url {:?}", url
            );
            assert_matches!(
                RelativePackageUrl::try_from(url),
                Err(e) if e == err,
                "the url {:?}", url
            );
            assert_matches!(
                serde_json::from_str::<RelativePackageUrl>(url),
                Err(_),
                "the url {:?}",
                url
            );
        }
    }

    #[test]
    fn parse_ok() {
        for url in ["name", "other3-name", "/name"] {
            let normalized_url = url.trim_start_matches('/');
            let json_url = format!("\"{url}\"");
            let normalized_json_url = format!("\"{normalized_url}\"");

            // Creation
            let name = normalized_url.parse::<crate::PackageName>().unwrap();
            let validate = |parsed: &RelativePackageUrl| {
                assert_eq!(parsed.path, name);
            };
            validate(&RelativePackageUrl::parse(url).unwrap());
            validate(&url.parse::<RelativePackageUrl>().unwrap());
            validate(&RelativePackageUrl::try_from(url).unwrap());
            validate(&serde_json::from_str::<RelativePackageUrl>(&json_url).unwrap());

            // Stringification
            assert_eq!(
                RelativePackageUrl::parse(url).unwrap().to_string(),
                normalized_url,
                "the url {:?}",
                url
            );
            assert_eq!(
                RelativePackageUrl::parse(url).unwrap().as_ref(),
                normalized_url,
                "the url {:?}",
                url
            );
            assert_eq!(
                serde_json::to_string(&RelativePackageUrl::parse(url).unwrap()).unwrap(),
                normalized_json_url,
                "the url {:?}",
                url
            );
        }
    }
}
