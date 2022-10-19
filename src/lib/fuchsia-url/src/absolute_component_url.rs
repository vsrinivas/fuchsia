// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::ParseError,
        parse::{validate_resource_path, PackageName, PackageVariant},
        AbsolutePackageUrl, RepositoryUrl, UrlParts,
    },
    fuchsia_hash::Hash,
};

/// A URL locating a Fuchsia component.
/// Has the form "fuchsia-pkg://<repository>/<name>[/variant][?hash=<hash>]#<resource>" where:
///   * "repository" is a valid hostname
///   * "name" is a valid package name
///   * "variant" is an optional valid package variant
///   * "hash" is an optional valid package hash
///   * "resource" is a valid resource path
/// https://fuchsia.dev/fuchsia-src/concepts/packages/package_url
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct AbsoluteComponentUrl {
    package: AbsolutePackageUrl,
    resource: String,
}

impl AbsoluteComponentUrl {
    /// Create an AbsoluteComponentUrl from its component parts.
    pub fn new(
        repo: RepositoryUrl,
        name: PackageName,
        variant: Option<PackageVariant>,
        hash: Option<Hash>,
        resource: String,
    ) -> Result<Self, ParseError> {
        let () = validate_resource_path(&resource).map_err(ParseError::InvalidResourcePath)?;
        Ok(Self { package: AbsolutePackageUrl::new(repo, name, variant, hash), resource })
    }

    pub(crate) fn from_parts(parts: UrlParts) -> Result<Self, ParseError> {
        let UrlParts { scheme, host, path, hash, resource } = parts;
        let repo = RepositoryUrl::new(
            scheme.ok_or(ParseError::MissingScheme)?,
            host.ok_or(ParseError::MissingHost)?,
        )?;
        let package = AbsolutePackageUrl::new_with_path(repo, &path, hash)?;
        let resource = resource.ok_or(ParseError::MissingResource)?;
        Ok(Self { package, resource })
    }

    /// Parse a "fuchsia-pkg://" URL that locates a component.
    pub fn parse(url: &str) -> Result<Self, ParseError> {
        Self::from_parts(UrlParts::parse(url)?)
    }

    /// Create an `AbsoluteComponentUrl` from a package URL and a resource path.
    pub fn from_package_url_and_resource(
        package: AbsolutePackageUrl,
        resource: String,
    ) -> Result<Self, ParseError> {
        let () = validate_resource_path(&resource).map_err(ParseError::InvalidResourcePath)?;
        Ok(Self { package, resource })
    }

    /// The resource path of this URL.
    pub fn resource(&self) -> &str {
        &self.resource
    }

    /// The package URL of this URL (this URL without the resource path).
    pub fn package_url(&self) -> &AbsolutePackageUrl {
        &self.package
    }

    pub(crate) fn into_package_and_resource(self) -> (AbsolutePackageUrl, String) {
        let Self { package, resource } = self;
        (package, resource)
    }
}

// AbsoluteComponentUrl does not maintain any invariants on its `package` field in addition to those
// already maintained by AbsolutePackageUrl so this is safe.
impl std::ops::Deref for AbsoluteComponentUrl {
    type Target = AbsolutePackageUrl;

    fn deref(&self) -> &Self::Target {
        &self.package
    }
}

// AbsoluteComponentUrl does not maintain any invariants on its `package` field in addition to those
// already maintained by AbsolutePackageUrl so this is safe.
impl std::ops::DerefMut for AbsoluteComponentUrl {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.package
    }
}

impl std::str::FromStr for AbsoluteComponentUrl {
    type Err = ParseError;

    fn from_str(url: &str) -> Result<Self, Self::Err> {
        Self::parse(url)
    }
}

impl std::convert::TryFrom<&str> for AbsoluteComponentUrl {
    type Error = ParseError;

    fn try_from(value: &str) -> Result<Self, Self::Error> {
        Self::parse(value)
    }
}

impl std::fmt::Display for AbsoluteComponentUrl {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "{}#{}",
            self.package,
            percent_encoding::utf8_percent_encode(&self.resource, crate::FRAGMENT)
        )
    }
}

impl serde::Serialize for AbsoluteComponentUrl {
    fn serialize<S: serde::Serializer>(&self, ser: S) -> Result<S::Ok, S::Error> {
        self.to_string().serialize(ser)
    }
}

impl<'de> serde::Deserialize<'de> for AbsoluteComponentUrl {
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
            ("example.org/name#resource", ParseError::MissingScheme),
            ("//example.org/name#resource", ParseError::MissingScheme),
            ("///name#resource", ParseError::MissingScheme),
            ("/name#resource", ParseError::MissingScheme),
            ("name#resource", ParseError::MissingScheme),
            ("fuchsia-boot://example.org/name#resource", ParseError::InvalidScheme),
            ("fuchsia-pkg:///name#resource", ParseError::MissingHost),
            ("fuchsia-pkg://exaMple.org/name#resource", ParseError::InvalidHost),
            ("fuchsia-pkg://example.org#resource", ParseError::MissingName),
            (
                "fuchsia-pkg://example.org//#resource",
                ParseError::InvalidPathSegment(PackagePathSegmentError::Empty),
            ),
            (
                "fuchsia-pkg://example.org/name/variant/extra#resource",
                ParseError::ExtraPathSegments,
            ),
            ("fuchsia-pkg://example.org/name#", ParseError::MissingResource),
            (
                "fuchsia-pkg://example.org/name#/",
                ParseError::InvalidResourcePath(ResourcePathError::PathStartsWithSlash),
            ),
            (
                "fuchsia-pkg://example.org/name#resource/",
                ParseError::InvalidResourcePath(ResourcePathError::PathEndsWithSlash),
            ),
        ] {
            assert_matches!(
                AbsoluteComponentUrl::parse(url),
                Err(e) if e == err,
                "the url {:?}", url
            );
            assert_matches!(
                url.parse::<AbsoluteComponentUrl>(),
                Err(e) if e == err,
                "the url {:?}", url
            );
            assert_matches!(
                AbsoluteComponentUrl::try_from(url),
                Err(e) if e == err,
                "the url {:?}", url
            );
            assert_matches!(
                serde_json::from_str::<AbsoluteComponentUrl>(url),
                Err(_),
                "the url {:?}",
                url
            );
        }
    }

    #[test]
    fn parse_ok() {
        for (url, variant, hash, resource) in [
            ("fuchsia-pkg://example.org/name#resource", None, None, "resource"),
            (
                "fuchsia-pkg://example.org/name/variant#resource",
                Some("variant"),
                None,
                "resource"
            ),
            ("fuchsia-pkg://example.org/name?hash=0000000000000000000000000000000000000000000000000000000000000000#resource", None, Some("0000000000000000000000000000000000000000000000000000000000000000"), "resource"),
            ("fuchsia-pkg://example.org/name#%E2%98%BA", None, None, "☺"),
        ] {
            let json_url = format!("\"{url}\"");
            let host = "example.org";
            let name = "name";

            // Creation
            let name = name.parse::<crate::PackageName>().unwrap();
            let variant = variant.map(|v| v.parse::<crate::PackageVariant>().unwrap());
            let hash = hash.map(|h| h.parse::<Hash>().unwrap());
            let validate = |parsed: &AbsoluteComponentUrl| {
                assert_eq!(parsed.host(), host);
                assert_eq!(parsed.name(), &name);
                assert_eq!(parsed.variant(), variant.as_ref());
                assert_eq!(parsed.hash(), hash);
                assert_eq!(parsed.resource(), resource);
            };
            validate(&AbsoluteComponentUrl::parse(url).unwrap());
            validate(&url.parse::<AbsoluteComponentUrl>().unwrap());
            validate(&AbsoluteComponentUrl::try_from(url).unwrap());
            validate(&serde_json::from_str::<AbsoluteComponentUrl>(&json_url).unwrap());

            // Stringification
            assert_eq!(
                AbsoluteComponentUrl::parse(url).unwrap().to_string(),
                url,
                "the url {:?}",
                url
            );
            assert_eq!(
                serde_json::to_string(&AbsoluteComponentUrl::parse(url).unwrap()).unwrap(),
                json_url,
                "the url {:?}",
                url
            );
        }
    }

    #[test]
    // Verify that resource path is validated at all, exhaustive testing of resource path
    // validation is performed by the tests on `validate_resource_path`.
    fn from_package_url_and_resource_err() {
        for (resource, err) in [
            ("", ParseError::InvalidResourcePath(ResourcePathError::PathIsEmpty)),
            ("/", ParseError::InvalidResourcePath(ResourcePathError::PathStartsWithSlash)),
        ] {
            let package = "fuchsia-pkg://example.org/name".parse::<AbsolutePackageUrl>().unwrap();
            assert_eq!(
                AbsoluteComponentUrl::from_package_url_and_resource(package, resource.into()),
                Err(err),
                "the resource {:?}",
                resource
            );
        }
    }

    #[test]
    fn from_package_url_and_resource_ok() {
        let package = "fuchsia-pkg://example.org/name".parse::<AbsolutePackageUrl>().unwrap();

        let component =
            AbsoluteComponentUrl::from_package_url_and_resource(package.clone(), "resource".into())
                .unwrap();
        assert_eq!(component.resource(), "resource");

        let component =
            AbsoluteComponentUrl::from_package_url_and_resource(package.clone(), "☺".into())
                .unwrap();
        assert_eq!(component.resource(), "☺");
    }
}
