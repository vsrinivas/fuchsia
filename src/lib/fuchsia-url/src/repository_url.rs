// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{errors::ParseError, Host, Scheme, UrlParts};

/// A URL locating a Fuchsia package repository.
/// Has the form "fuchsia-pkg://<repository>", where "repository" is a valid hostname.
/// https://fuchsia.dev/fuchsia-src/concepts/packages/package_url?hl=en#repository
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct RepositoryUrl {
    host: Host,
}

impl RepositoryUrl {
    /// Returns an error if the provided hostname does not comply to the package URL spec:
    /// https://fuchsia.dev/fuchsia-src/concepts/packages/package_url#repository
    /// Contains only lowercase ascii letters, digits, a hyphen or the dot delimiter.
    pub fn parse_host(host: String) -> Result<Self, ParseError> {
        Ok(Self { host: Host::parse(host)? })
    }

    /// Parse a "fuchsia-pkg://" URL that locates a package repository.
    pub fn parse(url: &str) -> Result<Self, ParseError> {
        let UrlParts { scheme, host, path, hash, resource } = UrlParts::parse(url)?;
        let scheme = scheme.ok_or(ParseError::MissingScheme)?;
        let host = host.ok_or(ParseError::MissingHost)?;
        if path != "/" {
            return Err(ParseError::ExtraPathSegments);
        }
        if hash.is_some() {
            return Err(ParseError::CannotContainHash);
        }
        if resource.is_some() {
            return Err(ParseError::CannotContainResource);
        }
        Self::new(scheme, host)
    }

    pub(crate) fn new(scheme: Scheme, host: Host) -> Result<Self, ParseError> {
        if scheme != Scheme::FuchsiaPkg {
            return Err(ParseError::InvalidScheme);
        }

        Ok(Self { host })
    }

    /// The hostname of the URL.
    pub fn host(&self) -> &str {
        self.host.as_ref()
    }

    /// Consumes the URL and returns the hostname.
    pub fn into_host(self) -> String {
        self.host.into()
    }

    /// Returns the channel name of the repository, if it exists.
    pub fn channel(&self) -> Option<&str> {
        self.host.as_ref().strip_suffix(".fuchsia.com")?.split('.').nth(1)
    }
}

impl std::str::FromStr for RepositoryUrl {
    type Err = ParseError;

    fn from_str(url: &str) -> Result<Self, Self::Err> {
        Self::parse(url)
    }
}

impl std::convert::TryFrom<&str> for RepositoryUrl {
    type Error = ParseError;

    fn try_from(value: &str) -> Result<Self, Self::Error> {
        Self::parse(value)
    }
}

impl std::fmt::Display for RepositoryUrl {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "fuchsia-pkg://{}", self.host.as_ref())
    }
}

impl serde::Serialize for RepositoryUrl {
    fn serialize<S: serde::Serializer>(&self, ser: S) -> Result<S::Ok, S::Error> {
        self.to_string().serialize(ser)
    }
}

impl<'de> serde::Deserialize<'de> for RepositoryUrl {
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
            ("example.org", ParseError::MissingScheme),
            ("fuchsia-boot://example.org", ParseError::InvalidScheme),
            ("fuchsia-pkg://", ParseError::MissingHost),
            ("fuchsia-pkg://exaMple.org", ParseError::InvalidHost),
            ("fuchsia-pkg://example.org/path", ParseError::ExtraPathSegments),
            ("fuchsia-pkg://example.org//", ParseError::InvalidPathSegment(PackagePathSegmentError::Empty)),
            ("fuchsia-pkg://example.org?hash=0000000000000000000000000000000000000000000000000000000000000000", ParseError::CannotContainHash),
            ("fuchsia-pkg://example.org#resource", ParseError::CannotContainResource),
            ("fuchsia-pkg://example.org/#resource", ParseError::CannotContainResource),
        ] {
            assert_matches!(
                RepositoryUrl::parse(url),
                Err(e) if e == err,
                "the url {:?}", url
            );
            assert_matches!(
                url.parse::<RepositoryUrl>(),
                Err(e) if e == err,
                "the url {:?}", url
            );
            assert_matches!(
                RepositoryUrl::try_from(url),
                Err(e) if e == err,
                "the url {:?}", url
            );
            assert_matches!(
                serde_json::from_str::<RepositoryUrl>(url),
                Err(_),
                "the url {:?}", url
            );
        }
    }

    #[test]
    fn parse_ok() {
        for (url, host, display) in [
            ("fuchsia-pkg://example.org", "example.org", "fuchsia-pkg://example.org"),
            ("fuchsia-pkg://example.org/", "example.org", "fuchsia-pkg://example.org"),
            ("fuchsia-pkg://example", "example", "fuchsia-pkg://example"),
        ] {
            // Creation
            assert_eq!(RepositoryUrl::parse(url).unwrap().host(), host, "the url {:?}", url);
            assert_eq!(url.parse::<RepositoryUrl>().unwrap().host(), host, "the url {:?}", url);
            assert_eq!(RepositoryUrl::try_from(url).unwrap().host(), host, "the url {:?}", url);
            assert_eq!(
                serde_json::from_str::<RepositoryUrl>(&format!("\"{url}\"")).unwrap().host(),
                host,
                "the url {:?}",
                url
            );

            // Stringification
            assert_eq!(
                RepositoryUrl::parse(url).unwrap().to_string(),
                display,
                "the url {:?}",
                url
            );
            assert_eq!(
                serde_json::to_string(&RepositoryUrl::parse(url).unwrap()).unwrap(),
                format!("\"{display}\""),
                "the url {:?}",
                url
            );
        }
    }

    #[test]
    fn channel_err() {
        for s in vec![
            "devhost",
            "fuchsia.com",
            "example.com",
            "test.fuchsia.com",
            "test.example.com",
            "a.b-c.d.example.com",
            "ignore.channel.fuchsia.comx",
            "ignore.channel.fuchsia.com.evil.com",
        ] {
            assert_eq!(RepositoryUrl::parse_host(s.to_string()).unwrap().channel(), None);
        }
    }

    #[test]
    fn channel_ok() {
        assert_eq!(
            RepositoryUrl::parse_host("a.b-c.d.fuchsia.com".to_string()).unwrap().channel(),
            Some("b-c")
        );
        assert_eq!(
            RepositoryUrl::parse_host("test.fuchsia.com.fuchsia.com".to_string())
                .unwrap()
                .channel(),
            Some("fuchsia")
        );
    }
}
