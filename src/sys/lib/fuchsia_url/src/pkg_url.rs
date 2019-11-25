// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use crate::errors::ParseError;
pub use crate::parse::{check_resource, is_hash, is_name};
use percent_encoding::{self, percent_decode};
use serde::{Deserialize, Deserializer, Serialize, Serializer};
use std::convert::TryFrom;
use std::fmt;
use std::str;
use url::{Host, Url};

/// Decoded representation of a fuchsia-pkg URL.
///
/// Depending on which segments are included, the URL may identify a package
/// repository, a package within a repository (with optional variant and hash),
/// or a resource within a package.
///
/// Repository identifier:
/// - fuchsia-pkg://example.com/
///
/// Package identifier:
/// - fuchsia-pkg://example.com/some-package
/// - fuchsia-pkg://example.com/some-package/some-variant
/// - fuchsia-pkg://example.com/some-package/some-variant?hash=<some-hash>
/// - fuchsia-pkg://example.com/some-package/some-variant/<some-hash> (obsolete)
///
/// Resource identifier:
/// - fuchsia-pkg://example.com/some-package#path/to/resource
/// - fuchsia-pkg://example.com/some-package/some-variant#path/to/resource
/// - fuchsia-pkg://example.com/some-package/some-variant?hash=<some-hash>#path/to/resource
/// - fuchsia-pkg://example.com/some-package/some-variant/<some-hash>#path/to/resource (obsolete)
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct PkgUrl {
    repo: RepoUrl,
    path: String,
    hash: Option<String>,
    resource: Option<String>,
}

impl PkgUrl {
    pub fn parse(input: &str) -> Result<Self, ParseError> {
        let url = Url::parse(input)?;

        let scheme = url.scheme();
        if scheme != "fuchsia-pkg" {
            return Err(ParseError::InvalidScheme);
        }

        let host = if let Some(host) = url.host() {
            host.to_string()
        } else {
            return Err(ParseError::InvalidHost);
        };
        if host.is_empty() {
            return Err(ParseError::InvalidHost);
        }

        if url.port().is_some() {
            return Err(ParseError::CannotContainPort);
        }

        if !url.username().is_empty() {
            return Err(ParseError::CannotContainUsername);
        }

        if url.password().is_some() {
            return Err(ParseError::CannotContainPassword);
        }

        parse_path(url.path())?;

        let path = url.path().to_string();
        let hash = parse_query_pairs(url.query_pairs())?;

        let resource = if let Some(resource) = url.fragment() {
            let resource = match percent_decode(resource.as_bytes()).decode_utf8() {
                Ok(resource) => resource,
                Err(_) => {
                    return Err(ParseError::InvalidResourcePath);
                }
            };

            if resource.is_empty() {
                None
            } else if check_resource(&resource) {
                Some(resource.to_string())
            } else {
                return Err(ParseError::InvalidResourcePath);
            }
        } else {
            None
        };

        Ok(PkgUrl { repo: RepoUrl { host }, path, hash, resource })
    }

    pub fn host(&self) -> &str {
        &self.repo.host()
    }

    pub fn name(&self) -> Option<&str> {
        // path is always prefixed by a '/'.
        self.path[1..].split_terminator('/').nth(0)
    }

    pub fn variant(&self) -> Option<&str> {
        // path is always prefixed by a '/'.
        self.path[1..].split_terminator('/').nth(1)
    }

    /// Produce a string representation of the package referenced by this [PkgUrl].
    pub fn path(&self) -> &str {
        &self.path
    }

    pub fn package_hash(&self) -> Option<&str> {
        self.hash.as_ref().map(|s| &**s)
    }

    pub fn resource(&self) -> Option<&str> {
        self.resource.as_ref().map(|s| &**s)
    }

    /// Returns true if this URL only contains a hostname, and no other parameters. For example,
    /// fuchsia-pkg://fuchsia.com.
    pub fn is_repository(&self) -> bool {
        self.path == "/" && self.hash.is_none() && self.resource.is_none()
    }

    /// Returns the [RepoUrl] that corresponds to this package URL.
    pub fn repo(&self) -> &RepoUrl {
        &self.repo
    }

    /// Produce a new [PkgUrl] with any resource fragment stripped off.
    pub fn root_url(&self) -> PkgUrl {
        PkgUrl {
            repo: self.repo.clone(),
            path: self.path.clone(),
            hash: self.hash.clone(),
            resource: None,
        }
    }

    pub fn new_repository(host: String) -> Result<PkgUrl, ParseError> {
        Ok(PkgUrl { repo: RepoUrl::new(host)?, path: "/".to_string(), hash: None, resource: None })
    }

    pub fn new_package(
        host: String,
        path: String,
        hash: Option<String>,
    ) -> Result<PkgUrl, ParseError> {
        let repo = RepoUrl::new(host)?;

        let (name, variant) = parse_path(path.as_str())?;

        if name.is_none() {
            return Err(ParseError::InvalidName);
        }

        if let Some(ref h) = hash {
            if variant.is_none() {
                return Err(ParseError::InvalidVariant);
            }
            if !is_hash(h) {
                return Err(ParseError::InvalidHash);
            }
        }

        Ok(PkgUrl { repo, path, hash, resource: None })
    }

    pub fn new_resource(
        host: String,
        path: String,
        hash: Option<String>,
        resource: String,
    ) -> Result<PkgUrl, ParseError> {
        let mut url = PkgUrl::new_package(host, path, hash)?;
        if resource.is_empty() || !check_resource(&resource) {
            return Err(ParseError::InvalidResourcePath);
        }
        url.resource = Some(resource);
        Ok(url)
    }
}

impl fmt::Display for PkgUrl {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.repo)?;
        if self.path != "/" {
            write!(f, "{}", self.path)?;
        }
        if let Some(ref hash) = self.hash {
            write!(f, "?hash={}", hash)?;
        }

        if let Some(ref resource) = self.resource {
            write!(f, "#{}", percent_encoding::utf8_percent_encode(resource, crate::FRAGMENT))?;
        }

        Ok(())
    }
}

impl str::FromStr for PkgUrl {
    type Err = ParseError;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        PkgUrl::parse(value)
    }
}

impl TryFrom<&str> for PkgUrl {
    type Error = ParseError;

    fn try_from(value: &str) -> Result<Self, Self::Error> {
        PkgUrl::parse(value)
    }
}

impl Serialize for PkgUrl {
    fn serialize<S: Serializer>(&self, ser: S) -> Result<S::Ok, S::Error> {
        self.to_string().serialize(ser)
    }
}

impl<'de> Deserialize<'de> for PkgUrl {
    fn deserialize<D>(de: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let url = String::deserialize(de)?;
        Ok(PkgUrl::parse(&url).map_err(|err| serde::de::Error::custom(err))?)
    }
}

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct RepoUrl {
    host: String,
}

impl RepoUrl {
    pub fn new(host: String) -> Result<Self, ParseError> {
        if host.is_empty() {
            return Err(ParseError::InvalidHost);
        }

        Host::parse(&host)?;

        Ok(RepoUrl { host })
    }

    pub fn parse(input: &str) -> Result<Self, ParseError> {
        let url = PkgUrl::parse(input)?;
        RepoUrl::try_from(url)
    }

    pub fn host(&self) -> &str {
        &self.host
    }
}

impl str::FromStr for RepoUrl {
    type Err = ParseError;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        RepoUrl::parse(value)
    }
}

impl TryFrom<&str> for RepoUrl {
    type Error = ParseError;

    fn try_from(url: &str) -> Result<Self, Self::Error> {
        RepoUrl::parse(url)
    }
}

impl TryFrom<PkgUrl> for RepoUrl {
    type Error = ParseError;

    fn try_from(url: PkgUrl) -> Result<Self, Self::Error> {
        if url.is_repository() {
            Ok(url.repo)
        } else {
            Err(ParseError::InvalidRepository)
        }
    }
}

impl From<RepoUrl> for PkgUrl {
    fn from(url: RepoUrl) -> Self {
        PkgUrl { repo: url, path: "/".to_string(), hash: None, resource: None }
    }
}

impl fmt::Display for RepoUrl {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "fuchsia-pkg://{}", self.host)
    }
}

impl Serialize for RepoUrl {
    fn serialize<S: Serializer>(&self, ser: S) -> Result<S::Ok, S::Error> {
        self.to_string().serialize(ser)
    }
}

// Implement a custom deserializer to make sure we restrict RepositoryConfig.repo_url to actually
// be a repository URL.
impl<'de> Deserialize<'de> for RepoUrl {
    fn deserialize<D: Deserializer<'de>>(de: D) -> Result<Self, D::Error> {
        let url = PkgUrl::deserialize(de)?;
        Ok(RepoUrl::try_from(url).map_err(|err| serde::de::Error::custom(err))?)
    }
}

fn parse_path(mut path: &str) -> Result<(Option<&str>, Option<&str>), ParseError> {
    let mut name = None;
    let mut variant = None;

    if !path.starts_with('/') {
        return Err(ParseError::InvalidPath);
    }

    path = &path[1..];
    if !path.is_empty() {
        let mut iter = path.split('/').fuse();

        if let Some(s) = iter.next() {
            if is_name(s) {
                name = Some(s);
            } else {
                return Err(ParseError::InvalidName);
            }
        }

        if let Some(s) = iter.next() {
            if is_name(s) {
                variant = Some(s);
            } else {
                return Err(ParseError::InvalidVariant);
            }
        }

        if let Some(_) = iter.next() {
            return Err(ParseError::ExtraPathSegments);
        }
    }

    Ok((name, variant))
}

fn parse_query_pairs(pairs: url::form_urlencoded::Parse<'_>) -> Result<Option<String>, ParseError> {
    let mut query_hash = None;
    for (key, value) in pairs {
        if key == "hash" {
            if query_hash.is_some() {
                return Err(ParseError::InvalidHash);
            }
            if !is_hash(&value) {
                return Err(ParseError::InvalidHash);
            }
            query_hash = Some(value.to_string());
        } else {
            return Err(ParseError::ExtraQueryParameters);
        }
    }
    Ok(query_hash)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::convert::TryInto;

    macro_rules! test_parse_ok {
        (
            $(
                $test_name:ident => {
                    url = $pkg_url:expr,
                    host = $pkg_host:expr,
                    path = $pkg_path:expr,
                    name = $pkg_name:expr,
                    variant = $pkg_variant:expr,
                    hash = $pkg_hash:expr,
                    resource = $pkg_resource:expr,
                }
            )+
        ) => {
            $(
                mod $test_name {
                    use super::*;
                    #[test]
                    fn test_eq() {
                        let pkg_url = $pkg_url.to_string();
                        let url = PkgUrl::parse(&pkg_url);
                        assert_eq!(
                            url,
                            Ok(PkgUrl {
                                repo: RepoUrl {
                                    host: $pkg_host.to_string(),
                                },
                                path: $pkg_path.to_string(),
                                hash: $pkg_hash.map(|s: &str| s.to_string()),
                                resource: $pkg_resource.map(|s: &str| s.to_string()),
                            })
                        );

                        let url = url.unwrap();
                        assert_eq!(url.path(), $pkg_path);
                        assert_eq!(url.name(), $pkg_name);
                        assert_eq!(url.variant(), $pkg_variant);
                        assert_eq!(url.package_hash(), $pkg_hash);
                        assert_eq!(url.resource(), $pkg_resource);
                    }

                    #[test]
                    fn test_roundtrip() {
                        let pkg_url = $pkg_url.to_string();
                        let parsed = PkgUrl::parse(&pkg_url).unwrap();
                        let format_pkg_url = parsed.to_string();
                        assert_eq!(
                            PkgUrl::parse(&format_pkg_url),
                            Ok(parsed)
                        );
                    }
                }
            )+
        }
    }

    macro_rules! test_parse_err {
        (
            $(
                $test_name:ident => {
                    urls = $urls:expr,
                    err = $err:expr,
                }
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    for url in &$urls {
                        assert_eq!(
                            PkgUrl::parse(url),
                            Err($err),
                        );
                    }
                }
            )+
        }
    }

    macro_rules! test_format {
        (
            $(
                $test_name:ident => {
                    parsed = $parsed:expr,
                    formatted = $formatted:expr,
                }
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    assert_eq!(
                        format!("{}", $parsed),
                        $formatted
                    );
                }
            )+
        }
    }

    test_parse_ok! {
        test_parse_host => {
            url = "fuchsia-pkg://fuchsia.com",
            host = "fuchsia.com",
            path = "/",
            name = None,
            variant = None,
            hash = None,
            resource = None,
        }
        test_parse_host_name => {
            url = "fuchsia-pkg://fuchsia.com/fonts",
            host = "fuchsia.com",
            path = "/fonts",
            name = Some("fonts"),
            variant = None,
            hash = None,
            resource = None,
        }
        test_parse_host_name_special_chars => {
            url = "fuchsia-pkg://fuchsia.com/abc123-._",
            host = "fuchsia.com",
            path = "/abc123-._",
            name = Some("abc123-._"),
            variant = None,
            hash = None,
            resource = None,
        }
        test_parse_host_name_variant => {
            url = "fuchsia-pkg://fuchsia.com/fonts/stable",
            host = "fuchsia.com",
            path = "/fonts/stable",
            name = Some("fonts"),
            variant = Some("stable"),
            hash = None,
            resource = None,
        }
        test_parse_host_name_variant_hash_query => {
            url = "fuchsia-pkg://fuchsia.com/fonts/stable?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
            host = "fuchsia.com",
            path = "/fonts/stable",
            name = Some("fonts"),
            variant = Some("stable"),
            hash = Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a"),
            resource = None,
        }
        test_parse_host_name_hash_query => {
            url = "fuchsia-pkg://fuchsia.com/fonts?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
            host = "fuchsia.com",
            path = "/fonts",
            name = Some("fonts"),
            variant = None,
            hash = Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a"),
            resource = None,
        }
        test_parse_ignoring_empty_resource => {
            url = "fuchsia-pkg://fuchsia.com/fonts#",
            host = "fuchsia.com",
            path = "/fonts",
            name = Some("fonts"),
            variant = None,
            hash = None,
            resource = None,
        }
        test_parse_resource => {
            url = "fuchsia-pkg://fuchsia.com/fonts#foo/bar",
            host = "fuchsia.com",
            path = "/fonts",
            name = Some("fonts"),
            variant = None,
            hash = None,
            resource = Some("foo/bar"),
        }
        test_parse_resource_decodes_percent_encoding => {
            url = "fuchsia-pkg://fuchsia.com/fonts#foo%23bar",
            host = "fuchsia.com",
            path = "/fonts",
            name = Some("fonts"),
            variant = None,
            hash = None,
            resource = Some("foo#bar"),
        }
        test_parse_resource_ignores_nul_chars => {
            url = "fuchsia-pkg://fuchsia.com/fonts#foo\x00bar",
            host = "fuchsia.com",
            path = "/fonts",
            name = Some("fonts"),
            variant = None,
            hash = None,
            resource = Some("foobar"),
        }
        test_parse_resource_allows_encoded_control_chars => {
            url = "fuchsia-pkg://fuchsia.com/fonts#foo%09bar",
            host = "fuchsia.com",
            path = "/fonts",
            name = Some("fonts"),
            variant = None,
            hash = None,
            resource = Some("foo\tbar"),
        }
    }

    test_parse_err! {
        test_parse_host_cannot_be_absent => {
            urls = [
                "fuchsia-pkg://",
            ],
            err = ParseError::InvalidHost,
        }
        test_parse_host_cannot_be_empty => {
            urls = [
                "fuchsia-pkg:///",
            ],
            err = ParseError::InvalidHost,
        }
        test_parse_name_cannot_be_empty => {
            urls = [
                "fuchsia-pkg://fuchsia.com//",
            ],
            err = ParseError::InvalidName,
        }
        test_parse_name_cannot_be_longer_than_100_chars => {
            urls = [
                "fuchsia-pkg://fuchsia.com/12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901/",
            ],
            err = ParseError::InvalidName,
        }
        test_parse_name_cannot_have_invalid_characters => {
            urls = [
                "fuchsia-pkg://fuchsia.com/$",
                "fuchsia-pkg://fuchsia.com/foo$bar",
            ],
            err = ParseError::InvalidName,
        }
        test_parse_variant_cannot_have_invalid_characters => {
            urls = [
                "fuchsia-pkg://fuchsia.com/fonts/$",
                "fuchsia-pkg://fuchsia.com/fonts/foo$bar",
            ],
            err = ParseError::InvalidVariant,
        }
        test_parse_hash_cannot_be_empty => {
            urls = [
                "fuchsia-pkg://fuchsia.com/fonts/stable?hash=",
            ],
            err = ParseError::InvalidHash,
        }
        test_parse_hash_cannot_have_invalid_characters => {
            urls = [
                "fuchsia-pkg://fuchsia.com/fonts/stable?hash=8$e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
                "fuchsia-pkg://fuchsia.com/fonts/stable?hash=80E8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
            ],
            err = ParseError::InvalidHash,
        }
        test_parse_hash_must_be_64_chars => {
            urls = [
                "fuchsia-pkg://fuchsia.com/fonts/stable?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4",
                "fuchsia-pkg://fuchsia.com/fonts/stable?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4aa",
            ],
            err = ParseError::InvalidHash,
        }
        test_parse_path_cannot_have_extra_segments => {
            urls = [
                "fuchsia-pkg://fuchsia.com/fonts/stable/80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
                "fuchsia-pkg://fuchsia.com/fonts/stable/80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a/foo",
            ],
            err = ParseError::ExtraPathSegments,
        }
        test_parse_resource_cannot_be_slash => {
            urls = [
                "fuchsia-pkg://fuchsia.com/fonts#/",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_start_with_slash => {
            urls = [
                "fuchsia-pkg://fuchsia.com/fonts#/foo",
                "fuchsia-pkg://fuchsia.com/fonts#/foo/bar",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_end_with_slash => {
            urls = [
                "fuchsia-pkg://fuchsia.com/fonts#foo/",
                "fuchsia-pkg://fuchsia.com/fonts#foo/bar/",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_contain_dot_dot => {
            urls = [
                "fuchsia-pkg://fuchsia.com/fonts#foo/../bar",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_contain_empty_segments => {
            urls = [
                "fuchsia-pkg://fuchsia.com/fonts#foo//bar",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_contain_percent_encoded_nul_chars => {
            urls = [
                "fuchsia-pkg://fuchsia.com/fonts#foo%00bar",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_rejects_port => {
            urls = [
                "fuchsia-pkg://fuchsia.com:1234",
            ],
            err = ParseError::CannotContainPort,
        }
        test_parse_resource_rejects_username => {
            urls = [
                "fuchsia-pkg://user@fuchsia.com",
                "fuchsia-pkg://user:password@fuchsia.com",
            ],
            err = ParseError::CannotContainUsername,
        }
        test_parse_resource_rejects_password => {
            urls = [
                "fuchsia-pkg://:password@fuchsia.com",
            ],
            err = ParseError::CannotContainPassword,
        }
        test_parse_rejects_unknown_query_params => {
            urls = [
                "fuchsia-pkg://fuchsia.com?foo=bar",
            ],
            err = ParseError::ExtraQueryParameters,
        }
    }

    test_format! {
        test_format_repository_url => {
            parsed = PkgUrl::new_repository("fuchsia.com".to_string()).unwrap(),
            formatted = "fuchsia-pkg://fuchsia.com",
        }
        test_format_package_url => {
            parsed = PkgUrl::new_package(
                "fuchsia.com".to_string(),
                "/fonts".to_string(),
                None,
            ).unwrap(),
            formatted = "fuchsia-pkg://fuchsia.com/fonts",
        }
        test_format_package_url_with_variant => {
            parsed = PkgUrl::new_package(
                "fuchsia.com".to_string(),
                "/fonts/stable".to_string(),
                None,
            ).unwrap(),
            formatted = "fuchsia-pkg://fuchsia.com/fonts/stable",
        }
        test_format_package_url_with_hash => {
            parsed = PkgUrl::new_package(
                "fuchsia.com".to_string(),
                "/fonts/stable".to_string(),
                Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a".to_string()),
            ).unwrap(),
            formatted = "fuchsia-pkg://fuchsia.com/fonts/stable?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
        }
        test_format_resource_url => {
            parsed = PkgUrl::new_resource(
                "fuchsia.com".to_string(),
                "/fonts".to_string(),
                None,
                "foo<>bar".to_string(),
            ).unwrap(),
            formatted = "fuchsia-pkg://fuchsia.com/fonts#foo%3C%3Ebar",
        }
    }

    #[test]
    fn test_new_repository() {
        let url = PkgUrl::new_repository("fuchsia.com".to_string()).unwrap();
        assert_eq!("fuchsia.com", url.host());
        assert_eq!("/", url.path());
        assert_eq!(None, url.name());
        assert_eq!(None, url.variant());
        assert_eq!(None, url.package_hash());
        assert_eq!(None, url.resource());

        assert_eq!(PkgUrl::new_repository("".to_string()), Err(ParseError::InvalidHost));
    }

    #[test]
    fn test_new_package() {
        let url = PkgUrl::new_package(
            "fuchsia.com".to_string(),
            "/fonts/stable".to_string(),
            Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a".to_string()),
        )
        .unwrap();
        assert_eq!("fuchsia.com", url.host());
        assert_eq!("/fonts/stable", url.path());
        assert_eq!(Some("fonts"), url.name());
        assert_eq!(Some("stable"), url.variant());
        assert_eq!(
            Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a"),
            url.package_hash()
        );
        assert_eq!(None, url.resource());
        assert_eq!(url, url.root_url());

        assert_eq!(
            PkgUrl::new_package("".to_string(), "/fonts".to_string(), None),
            Err(ParseError::InvalidHost)
        );
        assert_eq!(
            PkgUrl::new_package("fuchsia.com".to_string(), "fonts".to_string(), None),
            Err(ParseError::InvalidPath)
        );
        assert_eq!(
            PkgUrl::new_package("fuchsia.com".to_string(), "/".to_string(), None),
            Err(ParseError::InvalidName)
        );
        assert_eq!(
            PkgUrl::new_package("fuchsia.com".to_string(), "/fonts/$".to_string(), None),
            Err(ParseError::InvalidVariant)
        );
        assert_eq!(
            PkgUrl::new_package(
                "fuchsia.com".to_string(),
                "/fonts".to_string(),
                Some(
                    "80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a".to_string()
                )
            ),
            Err(ParseError::InvalidVariant)
        );
        assert_eq!(
            PkgUrl::new_package(
                "fuchsia.com".to_string(),
                "/fonts/stable".to_string(),
                Some("$".to_string())
            ),
            Err(ParseError::InvalidHash)
        );
    }

    #[test]
    fn test_new_resource() {
        let url = PkgUrl::new_resource(
            "fuchsia.com".to_string(),
            "/fonts/stable".to_string(),
            Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a".to_string()),
            "foo/bar".to_string(),
        )
        .unwrap();
        assert_eq!("fuchsia.com", url.host());
        assert_eq!("/fonts/stable", url.path());
        assert_eq!(Some("fonts"), url.name());
        assert_eq!(Some("stable"), url.variant());
        assert_eq!(
            Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a"),
            url.package_hash()
        );
        assert_eq!(Some("foo/bar"), url.resource());
        let mut url_no_resource = url.clone();
        url_no_resource.resource = None;
        assert_eq!(url_no_resource, url.root_url());

        assert_eq!(
            PkgUrl::new_resource("".to_string(), "/fonts".to_string(), None, "foo/bar".to_string()),
            Err(ParseError::InvalidHost)
        );
        assert_eq!(
            PkgUrl::new_resource(
                "fuchsia.com".to_string(),
                "/".to_string(),
                None,
                "foo/bar".to_string()
            ),
            Err(ParseError::InvalidName)
        );
        assert_eq!(
            PkgUrl::new_resource(
                "fuchsia.com".to_string(),
                "/fonts/$".to_string(),
                None,
                "foo/bar".to_string()
            ),
            Err(ParseError::InvalidVariant)
        );
        assert_eq!(
            PkgUrl::new_resource(
                "fuchsia.com".to_string(),
                "/fonts".to_string(),
                Some(
                    "80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a".to_string()
                ),
                "foo/bar".to_string()
            ),
            Err(ParseError::InvalidVariant)
        );
        assert_eq!(
            PkgUrl::new_resource(
                "fuchsia.com".to_string(),
                "/fonts/stable".to_string(),
                Some("$".to_string()),
                "foo/bar".to_string()
            ),
            Err(ParseError::InvalidHash)
        );
        assert_eq!(
            PkgUrl::new_resource(
                "fuchsia.com".to_string(),
                "/fonts".to_string(),
                None,
                "".to_string()
            ),
            Err(ParseError::InvalidResourcePath)
        );
        assert_eq!(
            PkgUrl::new_resource(
                "fuchsia.com".to_string(),
                "/fonts".to_string(),
                None,
                "a//b".to_string()
            ),
            Err(ParseError::InvalidResourcePath)
        );
    }

    #[test]
    fn test_repo_url() {
        let parsed_pkg_url = PkgUrl::new_repository("fuchsia.com".to_string()).unwrap();
        let parsed_repo_url = RepoUrl::new("fuchsia.com".to_string()).unwrap();

        let urls = &["fuchsia-pkg://fuchsia.com", "fuchsia-pkg://fuchsia.com/"];
        for url in urls {
            let url = RepoUrl::parse(url);
            assert_eq!(url.as_ref(), Ok(&parsed_repo_url));

            let url = url.unwrap();
            assert_eq!(url.host(), "fuchsia.com");

            assert_eq!(url.try_into().as_ref(), Ok(&parsed_pkg_url));
        }

        let urls = &[
            "fuchsia-pkg://fuchsia.com/foo",
            "fuchsia-pkg://fuchsia.com/foo/0",
            "fuchsia-pkg://fuchsia.com#bar",
            "fuchsia-pkg://fuchsia.com?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
        ];
        for url in urls {
            assert_eq!(RepoUrl::parse(url), Err(ParseError::InvalidRepository));
        }
    }
}
