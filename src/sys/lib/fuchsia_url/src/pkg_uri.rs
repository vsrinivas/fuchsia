// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use crate::errors::ParseError;
pub use crate::parse::{check_resource, is_hash, is_name};
use serde::{Deserialize, Deserializer, Serialize, Serializer};
use std::convert::TryFrom;
use std::fmt;
use std::str;
use url::percent_encoding::percent_decode;
use url::{Host, Url};

/// Decoded representation of a fuchsia-pkg URI.
///
/// Depending on which segments are included, the URI may identify a package
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
pub struct PkgUri {
    repo: RepoUri,
    path: String,
    hash: Option<String>,
    resource: Option<String>,
}

impl PkgUri {
    pub fn parse(input: &str) -> Result<Self, ParseError> {
        let uri = Url::parse(input)?;

        let scheme = uri.scheme();
        if scheme != "fuchsia-pkg" {
            return Err(ParseError::InvalidScheme);
        }

        let host = if let Some(host) = uri.host() {
            host.to_string()
        } else {
            return Err(ParseError::InvalidHost);
        };
        if host.is_empty() {
            return Err(ParseError::InvalidHost);
        }

        if uri.port().is_some() {
            return Err(ParseError::CannotContainPort);
        }

        if !uri.username().is_empty() {
            return Err(ParseError::CannotContainUsername);
        }

        if uri.password().is_some() {
            return Err(ParseError::CannotContainPassword);
        }

        parse_path(uri.path())?;

        let path = uri.path().to_string();
        let hash = parse_query_pairs(uri.query_pairs())?;

        let resource = if let Some(resource) = uri.fragment() {
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

        Ok(PkgUri { repo: RepoUri { host }, path, hash, resource })
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

    /// Produce a string representation of the package referenced by this [PkgUri].
    pub fn path(&self) -> &str {
        &self.path
    }

    pub fn package_hash(&self) -> Option<&str> {
        self.hash.as_ref().map(|s| &**s)
    }

    pub fn resource(&self) -> Option<&str> {
        self.resource.as_ref().map(|s| &**s)
    }

    /// Returns true if this URI only contains a hostname, and no other parameters. For example,
    /// fuchsia-pkg://fuchsia.com.
    pub fn is_repository(&self) -> bool {
        self.path == "/" && self.hash.is_none() && self.resource.is_none()
    }

    /// Returns the [RepoUri] that corresponds to this package URI.
    pub fn repo(&self) -> &RepoUri {
        &self.repo
    }

    /// Produce a new [PkgUri] with any resource fragment stripped off.
    pub fn root_uri(&self) -> PkgUri {
        PkgUri {
            repo: self.repo.clone(),
            path: self.path.clone(),
            hash: self.hash.clone(),
            resource: None,
        }
    }

    pub fn new_repository(host: String) -> Result<PkgUri, ParseError> {
        Ok(PkgUri { repo: RepoUri::new(host)?, path: "/".to_string(), hash: None, resource: None })
    }

    pub fn new_package(
        host: String,
        path: String,
        hash: Option<String>,
    ) -> Result<PkgUri, ParseError> {
        let repo = RepoUri::new(host)?;

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

        Ok(PkgUri { repo, path, hash, resource: None })
    }

    pub fn new_resource(
        host: String,
        path: String,
        hash: Option<String>,
        resource: String,
    ) -> Result<PkgUri, ParseError> {
        let mut uri = PkgUri::new_package(host, path, hash)?;
        if resource.is_empty() || !check_resource(&resource) {
            return Err(ParseError::InvalidResourcePath);
        }
        uri.resource = Some(resource);
        Ok(uri)
    }
}

impl fmt::Display for PkgUri {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}", self.repo)?;
        if self.path != "/" {
            write!(f, "{}", self.path)?;
        }
        if let Some(ref hash) = self.hash {
            write!(f, "?hash={}", hash)?;
        }

        if let Some(ref resource) = self.resource {
            write!(
                f,
                "#{}",
                url::percent_encoding::utf8_percent_encode(
                    resource,
                    url::percent_encoding::DEFAULT_ENCODE_SET
                )
            )?;
        }

        Ok(())
    }
}

impl str::FromStr for PkgUri {
    type Err = ParseError;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        PkgUri::parse(value)
    }
}

impl TryFrom<&str> for PkgUri {
    type Error = ParseError;

    fn try_from(value: &str) -> Result<Self, Self::Error> {
        PkgUri::parse(value)
    }
}

impl Serialize for PkgUri {
    fn serialize<S: Serializer>(&self, ser: S) -> Result<S::Ok, S::Error> {
        self.to_string().serialize(ser)
    }
}

impl<'de> Deserialize<'de> for PkgUri {
    fn deserialize<D>(de: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let uri = String::deserialize(de)?;
        Ok(PkgUri::parse(&uri).map_err(|err| serde::de::Error::custom(err))?)
    }
}

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct RepoUri {
    host: String,
}

impl RepoUri {
    pub fn new(host: String) -> Result<Self, ParseError> {
        if host.is_empty() {
            return Err(ParseError::InvalidHost);
        }

        Host::parse(&host)?;

        Ok(RepoUri { host })
    }

    pub fn parse(input: &str) -> Result<Self, ParseError> {
        let uri = PkgUri::parse(input)?;
        RepoUri::try_from(uri)
    }

    pub fn host(&self) -> &str {
        &self.host
    }
}

impl str::FromStr for RepoUri {
    type Err = ParseError;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        RepoUri::parse(value)
    }
}

impl TryFrom<&str> for RepoUri {
    type Error = ParseError;

    fn try_from(uri: &str) -> Result<Self, Self::Error> {
        RepoUri::parse(uri)
    }
}

impl TryFrom<PkgUri> for RepoUri {
    type Error = ParseError;

    fn try_from(uri: PkgUri) -> Result<Self, Self::Error> {
        if uri.is_repository() {
            Ok(uri.repo)
        } else {
            Err(ParseError::InvalidRepository)
        }
    }
}

impl From<RepoUri> for PkgUri {
    fn from(uri: RepoUri) -> Self {
        PkgUri { repo: uri, path: "/".to_string(), hash: None, resource: None }
    }
}

impl fmt::Display for RepoUri {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "fuchsia-pkg://{}", self.host)
    }
}

impl Serialize for RepoUri {
    fn serialize<S: Serializer>(&self, ser: S) -> Result<S::Ok, S::Error> {
        self.to_string().serialize(ser)
    }
}

// Implement a custom deserializer to make sure we restrict RepositoryConfig.repo_url to actually
// be a repository URI.
impl<'de> Deserialize<'de> for RepoUri {
    fn deserialize<D: Deserializer<'de>>(de: D) -> Result<Self, D::Error> {
        let uri = PkgUri::deserialize(de)?;
        Ok(RepoUri::try_from(uri).map_err(|err| serde::de::Error::custom(err))?)
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

fn parse_query_pairs(pairs: url::form_urlencoded::Parse) -> Result<Option<String>, ParseError> {
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
                    uri = $pkg_uri:expr,
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
                        let pkg_uri = $pkg_uri.to_string();
                        let uri = PkgUri::parse(&pkg_uri);
                        assert_eq!(
                            uri,
                            Ok(PkgUri {
                                repo: RepoUri {
                                    host: $pkg_host.to_string(),
                                },
                                path: $pkg_path.to_string(),
                                hash: $pkg_hash.map(|s: &str| s.to_string()),
                                resource: $pkg_resource.map(|s: &str| s.to_string()),
                            })
                        );

                        let uri = uri.unwrap();
                        assert_eq!(uri.path(), $pkg_path);
                        assert_eq!(uri.name(), $pkg_name);
                        assert_eq!(uri.variant(), $pkg_variant);
                        assert_eq!(uri.package_hash(), $pkg_hash);
                        assert_eq!(uri.resource(), $pkg_resource);
                    }

                    #[test]
                    fn test_roundtrip() {
                        let pkg_uri = $pkg_uri.to_string();
                        let parsed = PkgUri::parse(&pkg_uri).unwrap();
                        let format_pkg_uri = parsed.to_string();
                        assert_eq!(
                            PkgUri::parse(&format_pkg_uri),
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
                    uris = $uris:expr,
                    err = $err:expr,
                }
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    for uri in &$uris {
                        assert_eq!(
                            PkgUri::parse(uri),
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
            uri = "fuchsia-pkg://fuchsia.com",
            host = "fuchsia.com",
            path = "/",
            name = None,
            variant = None,
            hash = None,
            resource = None,
        }
        test_parse_host_name => {
            uri = "fuchsia-pkg://fuchsia.com/fonts",
            host = "fuchsia.com",
            path = "/fonts",
            name = Some("fonts"),
            variant = None,
            hash = None,
            resource = None,
        }
        test_parse_host_name_special_chars => {
            uri = "fuchsia-pkg://fuchsia.com/abc123-._",
            host = "fuchsia.com",
            path = "/abc123-._",
            name = Some("abc123-._"),
            variant = None,
            hash = None,
            resource = None,
        }
        test_parse_host_name_variant => {
            uri = "fuchsia-pkg://fuchsia.com/fonts/stable",
            host = "fuchsia.com",
            path = "/fonts/stable",
            name = Some("fonts"),
            variant = Some("stable"),
            hash = None,
            resource = None,
        }
        test_parse_host_name_variant_hash_query => {
            uri = "fuchsia-pkg://fuchsia.com/fonts/stable?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
            host = "fuchsia.com",
            path = "/fonts/stable",
            name = Some("fonts"),
            variant = Some("stable"),
            hash = Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a"),
            resource = None,
        }
        test_parse_host_name_hash_query => {
            uri = "fuchsia-pkg://fuchsia.com/fonts?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
            host = "fuchsia.com",
            path = "/fonts",
            name = Some("fonts"),
            variant = None,
            hash = Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a"),
            resource = None,
        }
        test_parse_ignoring_empty_resource => {
            uri = "fuchsia-pkg://fuchsia.com/fonts#",
            host = "fuchsia.com",
            path = "/fonts",
            name = Some("fonts"),
            variant = None,
            hash = None,
            resource = None,
        }
        test_parse_resource => {
            uri = "fuchsia-pkg://fuchsia.com/fonts#foo/bar",
            host = "fuchsia.com",
            path = "/fonts",
            name = Some("fonts"),
            variant = None,
            hash = None,
            resource = Some("foo/bar"),
        }
        test_parse_resource_decodes_percent_encoding => {
            uri = "fuchsia-pkg://fuchsia.com/fonts#foo%23bar",
            host = "fuchsia.com",
            path = "/fonts",
            name = Some("fonts"),
            variant = None,
            hash = None,
            resource = Some("foo#bar"),
        }
        test_parse_resource_ignores_nul_chars => {
            uri = "fuchsia-pkg://fuchsia.com/fonts#foo\x00bar",
            host = "fuchsia.com",
            path = "/fonts",
            name = Some("fonts"),
            variant = None,
            hash = None,
            resource = Some("foobar"),
        }
        test_parse_resource_allows_encoded_control_chars => {
            uri = "fuchsia-pkg://fuchsia.com/fonts#foo%09bar",
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
            uris = [
                "fuchsia-pkg://",
            ],
            err = ParseError::InvalidHost,
        }
        test_parse_host_cannot_be_empty => {
            uris = [
                "fuchsia-pkg:///",
            ],
            err = ParseError::InvalidHost,
        }
        test_parse_name_cannot_be_empty => {
            uris = [
                "fuchsia-pkg://fuchsia.com//",
            ],
            err = ParseError::InvalidName,
        }
        test_parse_name_cannot_be_longer_than_100_chars => {
            uris = [
                "fuchsia-pkg://fuchsia.com/12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901/",
            ],
            err = ParseError::InvalidName,
        }
        test_parse_name_cannot_have_invalid_characters => {
            uris = [
                "fuchsia-pkg://fuchsia.com/$",
                "fuchsia-pkg://fuchsia.com/foo$bar",
            ],
            err = ParseError::InvalidName,
        }
        test_parse_variant_cannot_have_invalid_characters => {
            uris = [
                "fuchsia-pkg://fuchsia.com/fonts/$",
                "fuchsia-pkg://fuchsia.com/fonts/foo$bar",
            ],
            err = ParseError::InvalidVariant,
        }
        test_parse_hash_cannot_be_empty => {
            uris = [
                "fuchsia-pkg://fuchsia.com/fonts/stable?hash=",
            ],
            err = ParseError::InvalidHash,
        }
        test_parse_hash_cannot_have_invalid_characters => {
            uris = [
                "fuchsia-pkg://fuchsia.com/fonts/stable?hash=8$e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
                "fuchsia-pkg://fuchsia.com/fonts/stable?hash=80E8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
            ],
            err = ParseError::InvalidHash,
        }
        test_parse_hash_must_be_64_chars => {
            uris = [
                "fuchsia-pkg://fuchsia.com/fonts/stable?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4",
                "fuchsia-pkg://fuchsia.com/fonts/stable?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4aa",
            ],
            err = ParseError::InvalidHash,
        }
        test_parse_path_cannot_have_extra_segments => {
            uris = [
                "fuchsia-pkg://fuchsia.com/fonts/stable/80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
                "fuchsia-pkg://fuchsia.com/fonts/stable/80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a/foo",
            ],
            err = ParseError::ExtraPathSegments,
        }
        test_parse_resource_cannot_be_slash => {
            uris = [
                "fuchsia-pkg://fuchsia.com/fonts#/",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_start_with_slash => {
            uris = [
                "fuchsia-pkg://fuchsia.com/fonts#/foo",
                "fuchsia-pkg://fuchsia.com/fonts#/foo/bar",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_end_with_slash => {
            uris = [
                "fuchsia-pkg://fuchsia.com/fonts#foo/",
                "fuchsia-pkg://fuchsia.com/fonts#foo/bar/",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_contain_dot_dot => {
            uris = [
                "fuchsia-pkg://fuchsia.com/fonts#foo/../bar",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_contain_empty_segments => {
            uris = [
                "fuchsia-pkg://fuchsia.com/fonts#foo//bar",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_contain_percent_encoded_nul_chars => {
            uris = [
                "fuchsia-pkg://fuchsia.com/fonts#foo%00bar",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_rejects_port => {
            uris = [
                "fuchsia-pkg://fuchsia.com:1234",
            ],
            err = ParseError::CannotContainPort,
        }
        test_parse_resource_rejects_username => {
            uris = [
                "fuchsia-pkg://user@fuchsia.com",
                "fuchsia-pkg://user:password@fuchsia.com",
            ],
            err = ParseError::CannotContainUsername,
        }
        test_parse_resource_rejects_password => {
            uris = [
                "fuchsia-pkg://:password@fuchsia.com",
            ],
            err = ParseError::CannotContainPassword,
        }
        test_parse_rejects_unknown_query_params => {
            uris = [
                "fuchsia-pkg://fuchsia.com?foo=bar",
            ],
            err = ParseError::ExtraQueryParameters,
        }
    }

    test_format! {
        test_format_repository_uri => {
            parsed = PkgUri::new_repository("fuchsia.com".to_string()).unwrap(),
            formatted = "fuchsia-pkg://fuchsia.com",
        }
        test_format_package_uri => {
            parsed = PkgUri::new_package(
                "fuchsia.com".to_string(),
                "/fonts".to_string(),
                None,
            ).unwrap(),
            formatted = "fuchsia-pkg://fuchsia.com/fonts",
        }
        test_format_package_uri_with_variant => {
            parsed = PkgUri::new_package(
                "fuchsia.com".to_string(),
                "/fonts/stable".to_string(),
                None,
            ).unwrap(),
            formatted = "fuchsia-pkg://fuchsia.com/fonts/stable",
        }
        test_format_package_uri_with_hash => {
            parsed = PkgUri::new_package(
                "fuchsia.com".to_string(),
                "/fonts/stable".to_string(),
                Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a".to_string()),
            ).unwrap(),
            formatted = "fuchsia-pkg://fuchsia.com/fonts/stable?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
        }
        test_format_resource_uri => {
            parsed = PkgUri::new_resource(
                "fuchsia.com".to_string(),
                "/fonts".to_string(),
                None,
                "foo#bar".to_string(),
            ).unwrap(),
            formatted = "fuchsia-pkg://fuchsia.com/fonts#foo%23bar",
        }
    }

    #[test]
    fn test_new_repository() {
        let uri = PkgUri::new_repository("fuchsia.com".to_string()).unwrap();
        assert_eq!("fuchsia.com", uri.host());
        assert_eq!("/", uri.path());
        assert_eq!(None, uri.name());
        assert_eq!(None, uri.variant());
        assert_eq!(None, uri.package_hash());
        assert_eq!(None, uri.resource());

        assert_eq!(PkgUri::new_repository("".to_string()), Err(ParseError::InvalidHost));
    }

    #[test]
    fn test_new_package() {
        let uri = PkgUri::new_package(
            "fuchsia.com".to_string(),
            "/fonts/stable".to_string(),
            Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a".to_string()),
        )
        .unwrap();
        assert_eq!("fuchsia.com", uri.host());
        assert_eq!("/fonts/stable", uri.path());
        assert_eq!(Some("fonts"), uri.name());
        assert_eq!(Some("stable"), uri.variant());
        assert_eq!(
            Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a"),
            uri.package_hash()
        );
        assert_eq!(None, uri.resource());
        assert_eq!(uri, uri.root_uri());

        assert_eq!(
            PkgUri::new_package("".to_string(), "/fonts".to_string(), None),
            Err(ParseError::InvalidHost)
        );
        assert_eq!(
            PkgUri::new_package("fuchsia.com".to_string(), "fonts".to_string(), None),
            Err(ParseError::InvalidPath)
        );
        assert_eq!(
            PkgUri::new_package("fuchsia.com".to_string(), "/".to_string(), None),
            Err(ParseError::InvalidName)
        );
        assert_eq!(
            PkgUri::new_package("fuchsia.com".to_string(), "/fonts/$".to_string(), None),
            Err(ParseError::InvalidVariant)
        );
        assert_eq!(
            PkgUri::new_package(
                "fuchsia.com".to_string(),
                "/fonts".to_string(),
                Some(
                    "80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a".to_string()
                )
            ),
            Err(ParseError::InvalidVariant)
        );
        assert_eq!(
            PkgUri::new_package(
                "fuchsia.com".to_string(),
                "/fonts/stable".to_string(),
                Some("$".to_string())
            ),
            Err(ParseError::InvalidHash)
        );
    }

    #[test]
    fn test_new_resource() {
        let uri = PkgUri::new_resource(
            "fuchsia.com".to_string(),
            "/fonts/stable".to_string(),
            Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a".to_string()),
            "foo/bar".to_string(),
        )
        .unwrap();
        assert_eq!("fuchsia.com", uri.host());
        assert_eq!("/fonts/stable", uri.path());
        assert_eq!(Some("fonts"), uri.name());
        assert_eq!(Some("stable"), uri.variant());
        assert_eq!(
            Some("80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a"),
            uri.package_hash()
        );
        assert_eq!(Some("foo/bar"), uri.resource());
        let mut uri_no_resource = uri.clone();
        uri_no_resource.resource = None;
        assert_eq!(uri_no_resource, uri.root_uri());

        assert_eq!(
            PkgUri::new_resource("".to_string(), "/fonts".to_string(), None, "foo/bar".to_string()),
            Err(ParseError::InvalidHost)
        );
        assert_eq!(
            PkgUri::new_resource(
                "fuchsia.com".to_string(),
                "/".to_string(),
                None,
                "foo/bar".to_string()
            ),
            Err(ParseError::InvalidName)
        );
        assert_eq!(
            PkgUri::new_resource(
                "fuchsia.com".to_string(),
                "/fonts/$".to_string(),
                None,
                "foo/bar".to_string()
            ),
            Err(ParseError::InvalidVariant)
        );
        assert_eq!(
            PkgUri::new_resource(
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
            PkgUri::new_resource(
                "fuchsia.com".to_string(),
                "/fonts/stable".to_string(),
                Some("$".to_string()),
                "foo/bar".to_string()
            ),
            Err(ParseError::InvalidHash)
        );
        assert_eq!(
            PkgUri::new_resource(
                "fuchsia.com".to_string(),
                "/fonts".to_string(),
                None,
                "".to_string()
            ),
            Err(ParseError::InvalidResourcePath)
        );
        assert_eq!(
            PkgUri::new_resource(
                "fuchsia.com".to_string(),
                "/fonts".to_string(),
                None,
                "a//b".to_string()
            ),
            Err(ParseError::InvalidResourcePath)
        );
    }

    #[test]
    fn test_repo_uri() {
        let parsed_pkg_uri = PkgUri::new_repository("fuchsia.com".to_string()).unwrap();
        let parsed_repo_uri = RepoUri::new("fuchsia.com".to_string()).unwrap();

        let uris = &["fuchsia-pkg://fuchsia.com", "fuchsia-pkg://fuchsia.com/"];
        for uri in uris {
            let uri = RepoUri::parse(uri);
            assert_eq!(uri.as_ref(), Ok(&parsed_repo_uri));

            let uri = uri.unwrap();
            assert_eq!(uri.host(), "fuchsia.com");

            assert_eq!(uri.try_into().as_ref(), Ok(&parsed_pkg_uri));
        }

        let uris = &[
            "fuchsia-pkg://fuchsia.com/foo",
            "fuchsia-pkg://fuchsia.com/foo/0",
            "fuchsia-pkg://fuchsia.com#bar",
            "fuchsia-pkg://fuchsia.com?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a",
        ];
        for uri in uris {
            assert_eq!(RepoUri::parse(uri), Err(ParseError::InvalidRepository));
        }
    }
}
