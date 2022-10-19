// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{errors::ParseError, RelativePackageUrl, UrlParts};

/// A relative URL locating a Fuchsia component. Used with a subpackage context.
/// Has the form "<name>#<resource>" where:
///   * "name" is a valid package name
///   * "resource" is a valid resource path
/// https://fuchsia.dev/fuchsia-src/concepts/packages/package_url
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct RelativeComponentUrl {
    package: RelativePackageUrl,
    resource: String,
}

impl RelativeComponentUrl {
    pub(crate) fn from_parts(parts: UrlParts) -> Result<Self, ParseError> {
        let UrlParts { scheme, host, path, hash, resource } = parts;
        let package =
            RelativePackageUrl::from_parts(UrlParts { scheme, host, path, hash, resource: None })?;
        let resource = resource.ok_or(ParseError::MissingResource)?;
        Ok(Self { package, resource: resource.to_string() })
    }

    /// Parse a relative component URL.
    pub fn parse(url: &str) -> Result<Self, ParseError> {
        Self::from_parts(UrlParts::parse(url)?)
    }

    /// The package URL of this URL (this URL without the resource path).
    pub fn package_url(&self) -> &RelativePackageUrl {
        &self.package
    }

    /// The resource path of this URL.
    pub fn resource(&self) -> &str {
        &self.resource
    }

    pub(crate) fn into_package_and_resource(self) -> (RelativePackageUrl, String) {
        let Self { package, resource } = self;
        (package, resource)
    }
}

impl std::str::FromStr for RelativeComponentUrl {
    type Err = ParseError;

    fn from_str(url: &str) -> Result<Self, Self::Err> {
        Self::parse(url)
    }
}

impl std::convert::TryFrom<&str> for RelativeComponentUrl {
    type Error = ParseError;

    fn try_from(value: &str) -> Result<Self, Self::Error> {
        Self::parse(value)
    }
}

impl std::fmt::Display for RelativeComponentUrl {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "{}#{}",
            self.package,
            percent_encoding::utf8_percent_encode(&self.resource, crate::FRAGMENT)
        )
    }
}

impl serde::Serialize for RelativeComponentUrl {
    fn serialize<S: serde::Serializer>(&self, ser: S) -> Result<S::Ok, S::Error> {
        self.to_string().serialize(ser)
    }
}

impl<'de> serde::Deserialize<'de> for RelativeComponentUrl {
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
        super::*,
        crate::errors::{PackagePathSegmentError, ResourcePathError},
        assert_matches::assert_matches,
        std::convert::TryFrom as _,
    };

    #[test]
    fn parse_err() {
        for (url, err) in [
            ("fuchsia-pkg://example.org/name#resource", ParseError::CannotContainScheme),
            ("fuchsia-pkg:///name#resource", ParseError::CannotContainScheme),
            ("fuchsia-pkg://name#resource", ParseError::CannotContainScheme),
            ("//example.org/name#resource", ParseError::HostMustBeEmpty),
            ("///name#resource", ParseError::HostMustBeEmpty),
            (
                "nAme#resource",
                ParseError::InvalidPathSegment(PackagePathSegmentError::InvalidCharacter {
                    character: 'A',
                }),
            ),
            ("name", ParseError::MissingResource),
            ("name#", ParseError::MissingResource),
            ("#resource", ParseError::MissingName),
            (".#resource", ParseError::MissingName),
            ("..#resource", ParseError::MissingName),
            (
                "name#resource/",
                ParseError::InvalidResourcePath(ResourcePathError::PathEndsWithSlash),
            ),
            ("name#..", ParseError::InvalidResourcePath(ResourcePathError::NameIsDotDot)),
            (
                "name#resource%00",
                ParseError::InvalidResourcePath(ResourcePathError::NameContainsNull),
            ),
            ("extra/segment#resource", ParseError::RelativePathCannotSpecifyVariant),
            ("too/many/segments#resource", ParseError::ExtraPathSegments),
        ] {
            assert_matches!(
                RelativeComponentUrl::parse(url),
                Err(e) if e == err,
                "the url {:?}",
                url
            );
            assert_matches!(
                url.parse::<RelativeComponentUrl>(),
                Err(e) if e == err,
                "the url {:?}",
                url
            );
            assert_matches!(
                RelativeComponentUrl::try_from(url),
                Err(e) if e == err,
                "the url {:?}",
                url
            );
            assert_matches!(
                serde_json::from_str::<RelativeComponentUrl>(url),
                Err(_),
                "the url {:?}",
                url
            );
        }
    }

    #[test]
    fn parse_ok() {
        for (url, package, resource) in [
            ("name#resource", "name", "resource"),
            ("name#reso%09urce", "name", "reso\turce"),
            ("/name#resource", "name", "resource"),
        ] {
            let normalized_url = url.trim_start_matches('/');
            let json_url = format!("\"{url}\"");
            let normalized_json_url = format!("\"{normalized_url}\"");

            // Creation
            let validate = |parsed: &RelativeComponentUrl| {
                assert_eq!(parsed.package_url().as_ref(), package);
                assert_eq!(parsed.resource(), resource);
            };
            validate(&RelativeComponentUrl::parse(url).unwrap());
            validate(&url.parse::<RelativeComponentUrl>().unwrap());
            validate(&RelativeComponentUrl::try_from(url).unwrap());
            validate(&serde_json::from_str::<RelativeComponentUrl>(&json_url).unwrap());

            // Stringification
            assert_eq!(RelativeComponentUrl::parse(url).unwrap().to_string(), normalized_url);
            assert_eq!(
                serde_json::to_string(&RelativeComponentUrl::parse(url).unwrap()).unwrap(),
                normalized_json_url,
            );
        }
    }
}
