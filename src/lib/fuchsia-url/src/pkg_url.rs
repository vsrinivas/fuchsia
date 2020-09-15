// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use crate::errors::ParseError;
pub use crate::parse::{check_resource, is_hash, is_name};
pub use fuchsia_hash::Hash;
use percent_encoding::{self, percent_decode};
use serde::{Deserialize, Deserializer, Serialize, Serializer};
use std::convert::TryFrom;
use std::fmt;
use std::str;
use url::{Host, Url};

/// Decoded representation of a fuchsia-pkg URL that identifies a package
/// and optionally, a resource in that package.
///
/// Package identifier:
/// - fuchsia-pkg://example.com/some-package
/// - fuchsia-pkg://example.com/some-package/some-variant
/// - fuchsia-pkg://example.com/some-package/some-variant?hash=<some-hash>
///
/// Resource identifier:
/// - fuchsia-pkg://example.com/some-package#path/to/resource
/// - fuchsia-pkg://example.com/some-package/some-variant#path/to/resource
/// - fuchsia-pkg://example.com/some-package/some-variant?hash=<some-hash>#path/to/resource
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct PkgUrl {
    repo: RepoUrl,
    path: String,
    hash: Option<Hash>,
    resource: Option<String>,
    name: String,
}

impl PkgUrl {
    pub fn parse(input: &str) -> Result<Self, ParseError> {
        let (url, repo_url) = parse_helper(input)?;

        let (name, _variant) = parse_path(url.path())?;

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

        Ok(PkgUrl { repo: repo_url, path, hash, resource, name: name.to_string() })
    }

    pub fn host(&self) -> &str {
        &self.repo.host()
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn variant(&self) -> Option<&str> {
        // path is always prefixed by a '/'.
        self.path[1..].split_terminator('/').nth(1)
    }

    /// Produce a string representation of the package referenced by this [PkgUrl].
    pub fn path(&self) -> &str {
        &self.path
    }

    pub fn package_hash(&self) -> Option<&Hash> {
        self.hash.as_ref()
    }

    pub fn resource(&self) -> Option<&str> {
        self.resource.as_ref().map(|s| &**s)
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
            name: self.name.clone(),
        }
    }

    /// Produce a new [PkgUrl] with any variant stripped off.
    pub fn strip_variant(&self) -> PkgUrl {
        PkgUrl {
            repo: self.repo.clone(),
            path: format!("/{}", self.name()),
            hash: self.hash.clone(),
            resource: self.resource.clone(),
            name: self.name().to_string(),
        }
    }

    pub fn new_package(
        host: String,
        path: String,
        hash: Option<Hash>,
    ) -> Result<PkgUrl, ParseError> {
        let repo = RepoUrl::new(host)?;
        let (name, _variant) = parse_path(path.as_str())?;
        let name = name.to_string();
        Ok(PkgUrl { repo, path, hash, resource: None, name })
    }

    pub fn new_resource(
        host: String,
        path: String,
        hash: Option<Hash>,
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

/// Decoded representation of a fuchsia-pkg URL that identifies a package repository.
///
/// Repository identifier:
/// - fuchsia-pkg://example.com/

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
        let (url, repo_url) = parse_helper(input)?;
        if url.path() != "/" || url.query().is_some() || url.fragment().is_some() {
            return Err(ParseError::InvalidRepository);
        }
        Ok(repo_url)
    }

    pub fn host(&self) -> &str {
        &self.host
    }

    /// Returns the channel name of the repo, if exists.
    pub fn channel(&self) -> Option<&str> {
        self.host.strip_suffix(".fuchsia.com")?.split('.').nth(1)
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
    fn deserialize<D>(de: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let url = String::deserialize(de)?;
        Ok(RepoUrl::parse(&url).map_err(|err| serde::de::Error::custom(err))?)
    }
}

// Validates the fuchsia-pkg URL invariants that are common between PkgUrl and
// RepoUrl.
fn parse_helper(input: &str) -> Result<(Url, RepoUrl), ParseError> {
    let url = Url::parse(input)?;

    let scheme = url.scheme();
    if scheme != "fuchsia-pkg" {
        return Err(ParseError::InvalidScheme);
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

    let host = if let Some(host) = url.host() {
        host.to_string()
    } else {
        return Err(ParseError::MissingHost);
    };
    if host.is_empty() {
        return Err(ParseError::EmptyHost);
    }

    Ok((url, RepoUrl { host }))
}

// Returns a valid name and an optional variant on success.
fn parse_path(mut path: &str) -> Result<(&str, Option<&str>), ParseError> {
    if !path.starts_with('/') {
        return Err(ParseError::InvalidPath);
    }
    path = &path[1..];
    if path.is_empty() {
        return Err(ParseError::MissingName);
    }
    let mut iter = path.split('/').fuse();
    let name = if let Some(s) = iter.next() {
        if is_name(s) {
            s
        } else {
            return Err(ParseError::InvalidName);
        }
    } else {
        return Err(ParseError::MissingName);
    };
    let variant = if let Some(s) = iter.next() {
        if is_name(s) {
            Some(s)
        } else {
            return Err(ParseError::InvalidVariant);
        }
    } else {
        None
    };
    if let Some(_) = iter.next() {
        return Err(ParseError::ExtraPathSegments);
    }
    Ok((name, variant))
}

fn parse_query_pairs(pairs: url::form_urlencoded::Parse<'_>) -> Result<Option<Hash>, ParseError> {
    let mut query_hash = None;
    for (key, value) in pairs {
        if key == "hash" {
            if query_hash.is_some() {
                return Err(ParseError::MultipleHashes);
            }
            query_hash = Some(value.parse().map_err(ParseError::InvalidHash)?);
            // fuchsia-pkg URLs require lowercase hex characters, which is not enforced by Hash.
            if !is_hash(&value) {
                return Err(ParseError::UpperCaseHash);
            }
        } else {
            return Err(ParseError::ExtraQueryParameters);
        }
    }
    Ok(query_hash)
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

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
                                hash: $pkg_hash.map(|s: &str| s.parse().unwrap()),
                                resource: $pkg_resource.map(|s: &str| s.to_string()),
                                name: $pkg_name.to_string()
                            })
                        );

                        let url = url.unwrap();
                        assert_eq!(url.path(), $pkg_path);
                        assert_eq!(url.name(), $pkg_name);
                        assert_eq!(url.variant(), $pkg_variant);
                        assert_eq!(url.package_hash(), $pkg_hash.map(|s: &str| s.parse().unwrap()).as_ref());
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
                    err = $err:pat,
                }
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    for url in &$urls {
                        assert_matches!(
                            PkgUrl::parse(url),
                            Err($err)
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
        test_parse_host_name => {
            url = "fuchsia-pkg://fuchsia.com/fonts",
            host = "fuchsia.com",
            path = "/fonts",
            name = "fonts",
            variant = None,
            hash = None,
            resource = None,
        }
        test_parse_host_name_special_chars => {
            url = "fuchsia-pkg://fuchsia.com/abc123-._",
            host = "fuchsia.com",
            path = "/abc123-._",
            name = "abc123-._",
            variant = None,
            hash = None,
            resource = None,
        }
        test_parse_host_name_variant => {
            url = "fuchsia-pkg://fuchsia.com/fonts/stable",
            host = "fuchsia.com",
            path = "/fonts/stable",
            name = "fonts",
            variant = Some("stable"),
            hash = None,
            resource = None,
        }
        test_parse_host_name_variant_hash_query => {
            url = "fuchsia-pkg://fuchsia.com/fonts/stable?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
            host = "fuchsia.com",
            path = "/fonts/stable",
            name = "fonts",
            variant = Some("stable"),
            hash = Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a"),
            resource = None,
        }
        test_parse_host_name_hash_query => {
            url = "fuchsia-pkg://fuchsia.com/fonts?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
            host = "fuchsia.com",
            path = "/fonts",
            name = "fonts",
            variant = None,
            hash = Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a"),
            resource = None,
        }
        test_parse_ignoring_empty_resource => {
            url = "fuchsia-pkg://fuchsia.com/fonts#",
            host = "fuchsia.com",
            path = "/fonts",
            name = "fonts",
            variant = None,
            hash = None,
            resource = None,
        }
        test_parse_resource => {
            url = "fuchsia-pkg://fuchsia.com/fonts#foo/bar",
            host = "fuchsia.com",
            path = "/fonts",
            name = "fonts",
            variant = None,
            hash = None,
            resource = Some("foo/bar"),
        }
        test_parse_resource_decodes_percent_encoding => {
            url = "fuchsia-pkg://fuchsia.com/fonts#foo%23bar",
            host = "fuchsia.com",
            path = "/fonts",
            name = "fonts",
            variant = None,
            hash = None,
            resource = Some("foo#bar"),
        }
        test_parse_resource_ignores_nul_chars => {
            url = "fuchsia-pkg://fuchsia.com/fonts#foo\x00bar",
            host = "fuchsia.com",
            path = "/fonts",
            name = "fonts",
            variant = None,
            hash = None,
            resource = Some("foobar"),
        }
        test_parse_resource_allows_encoded_control_chars => {
            url = "fuchsia-pkg://fuchsia.com/fonts#foo%09bar",
            host = "fuchsia.com",
            path = "/fonts",
            name = "fonts",
            variant = None,
            hash = None,
            resource = Some("foo\tbar"),
        }
    }

    test_parse_err! {
        test_parse_host_cannot_be_empty => {
            urls = [
                "fuchsia-pkg://",
            ],
            err = ParseError::EmptyHost,
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
            err = ParseError::InvalidHash(_),
        }
        test_parse_hash_cannot_have_invalid_characters => {
            urls = [
                "fuchsia-pkg://fuchsia.com/fonts/stable?hash=8$e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
            ],
            err = ParseError::InvalidHash(_),
        }
        test_parse_hash_cannot_have_uppercase_characters => {
            urls = [
                "fuchsia-pkg://fuchsia.com/fonts/stable?hash=80E8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
            ],
            err = ParseError::UpperCaseHash,
        }
        test_parse_hash_must_be_64_chars => {
            urls = [
                "fuchsia-pkg://fuchsia.com/fonts/stable?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4",
                "fuchsia-pkg://fuchsia.com/fonts/stable?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4aa",
            ],
            err = ParseError::InvalidHash(_),
        }
        test_parse_hash_cannot_have_multiple_hashes => {
            urls = [
                "fuchsia-pkg://fuchsia.com/fonts/stable?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a&hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
            ],
            err = ParseError::MultipleHashes,
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
                "fuchsia-pkg://fuchsia.com/name?foo=bar",
            ],
            err = ParseError::ExtraQueryParameters,
        }
    }

    test_format! {
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
                Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a").map(|s| s.parse().unwrap()),
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
    fn test_new_package() {
        let url = PkgUrl::new_package(
            "fuchsia.com".to_string(),
            "/fonts/stable".to_string(),
            Some(
                "80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a".parse().unwrap(),
            ),
        )
        .unwrap();
        assert_eq!("fuchsia.com", url.host());
        assert_eq!("/fonts/stable", url.path());
        assert_eq!("fonts", url.name());
        assert_eq!(Some("stable"), url.variant());
        assert_eq!(
            Some(
                "80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a".parse().unwrap()
            )
            .as_ref(),
            url.package_hash()
        );
        assert_eq!(None, url.resource());
        assert_eq!(url, url.root_url());

        assert_matches!(
            PkgUrl::new_package("".to_string(), "/fonts".to_string(), None),
            Err(ParseError::InvalidHost)
        );
        assert_matches!(
            PkgUrl::new_package("fuchsia.com".to_string(), "fonts".to_string(), None),
            Err(ParseError::InvalidPath)
        );
        assert_matches!(
            PkgUrl::new_package("fuchsia.com".to_string(), "/".to_string(), None),
            Err(ParseError::MissingName)
        );
        assert_matches!(
            PkgUrl::new_package("fuchsia.com".to_string(), "/fonts/$".to_string(), None),
            Err(ParseError::InvalidVariant)
        );
        let url = PkgUrl::new_package(
            "fuchsia.com".to_string(),
            "/fonts".to_string(),
            Some(
                "80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a".parse().unwrap(),
            ),
        )
        .unwrap();

        assert_eq!("fuchsia.com", url.host());
        assert_eq!("/fonts", url.path());
        assert_eq!("fonts", url.name());
        assert_eq!(None, url.variant());
        assert_eq!(
            Some(
                "80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a".parse().unwrap()
            )
            .as_ref(),
            url.package_hash()
        );
    }

    #[test]
    fn test_new_resource() {
        let url = PkgUrl::new_resource(
            "fuchsia.com".to_string(),
            "/fonts/stable".to_string(),
            Some(
                "80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a".parse().unwrap(),
            ),
            "foo/bar".to_string(),
        )
        .unwrap();
        assert_eq!("fuchsia.com", url.host());
        assert_eq!("/fonts/stable", url.path());
        assert_eq!("fonts", url.name());
        assert_eq!(Some("stable"), url.variant());
        assert_eq!(
            Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a")
                .map(|s| s.parse::<Hash>().unwrap())
                .as_ref(),
            url.package_hash(),
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
            Err(ParseError::MissingName)
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
        let url = PkgUrl::new_resource(
            "fuchsia.com".to_string(),
            "/fonts".to_string(),
            Some(
                "80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a".parse().unwrap(),
            ),
            "foo/bar".to_string(),
        )
        .unwrap();
        assert_eq!("fuchsia.com", url.host());
        assert_eq!("/fonts", url.path());
        assert_eq!("fonts", url.name());
        assert_eq!(None, url.variant());
        assert_eq!(
            Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a")
                .map(|s| s.parse::<Hash>().unwrap())
                .as_ref(),
            url.package_hash(),
        );
        assert_eq!(Some("foo/bar"), url.resource());

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
        let parsed_repo_url = RepoUrl::new("fuchsia.com".to_string()).unwrap();

        let urls = &["fuchsia-pkg://fuchsia.com", "fuchsia-pkg://fuchsia.com/"];
        for url in urls {
            let url = RepoUrl::parse(url);
            assert_eq!(url.as_ref(), Ok(&parsed_repo_url));

            let url = url.unwrap();
            assert_eq!(url.host(), "fuchsia.com");
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

    #[test]
    fn test_repo_url_channel() {
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
            assert_eq!(RepoUrl::new(s.to_string()).unwrap().channel(), None);
        }
        assert_eq!(RepoUrl::new("a.b-c.d.fuchsia.com".to_string()).unwrap().channel(), Some("b-c"));
        assert_eq!(
            RepoUrl::new("test.fuchsia.com.fuchsia.com".to_string()).unwrap().channel(),
            Some("fuchsia")
        );
    }

    #[test]
    fn test_strip_variant() {
        let urls = &["fuchsia-pkg://fuchsia.com/foo", "fuchsia-pkg://fuchsia.com/name#bar"];
        for url in urls {
            // Don't change urls with no variant.
            assert_eq!(PkgUrl::parse(url).unwrap(), PkgUrl::parse(url).unwrap().strip_variant());
        }

        let var_urls = &[
            "fuchsia-pkg://fuchsia.com/foo/0",
            "fuchsia-pkg://fuchsia.com/foo/0#bar",
            "fuchsia-pkg://fuchsia.com/foo/0?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
        ];
        let stripped_urls = &[
            "fuchsia-pkg://fuchsia.com/foo",
            "fuchsia-pkg://fuchsia.com/foo#bar",
            "fuchsia-pkg://fuchsia.com/foo?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
        ];

        for (i, want_url) in stripped_urls.iter().enumerate() {
            assert_eq!(
                PkgUrl::parse(var_urls[i]).unwrap().strip_variant(),
                PkgUrl::parse(want_url).unwrap()
            );
        }
    }
}
