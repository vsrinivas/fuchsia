// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use crate::errors::ParseError;
pub use crate::parse::{check_resource, is_name};
use std::fmt;
use url::percent_encoding::percent_decode;
use url::Url;

/// Decoded representation of a fuchsia-boot URI.
///
/// fuchsia-boot:///path/to#path/to/resource
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct BootUri {
    path: String,
    resource: Option<String>,
}

impl BootUri {
    pub fn parse(input: &str) -> Result<Self, ParseError> {
        let uri = Url::parse(input)?;

        let scheme = uri.scheme();
        if scheme != "fuchsia-boot" {
            return Err(ParseError::InvalidScheme);
        }

        let host = uri.host().map(|h| h.to_string()).ok_or(ParseError::InvalidHost)?;
        if !host.is_empty() {
            return Err(ParseError::HostMustBeEmpty);
        }

        let path = uri.path().to_string();

        let mut path_clone = path.as_str().clone();
        // Since host must be empty, first character of path must be '/'. Trim it.
        path_clone = &path_clone[1..];
        if !path_clone.is_empty() {
            let mut iter = path_clone.split('/').fuse();
            if let Some(s) = iter.next() {
                if !is_name(s) {
                    return Err(ParseError::InvalidPath);
                }
            }
            if let Some(_) = iter.next() {
                return Err(ParseError::ExtraPathSegments);
            }
        }

        if uri.query().is_some() {
            return Err(ParseError::CannotContainQueryParameters);
        }

        let resource = match uri.fragment() {
            Some(resource) => {
                let resource = percent_decode(resource.as_bytes())
                    .decode_utf8()
                    .map_err(|_| ParseError::InvalidResourcePath)?;

                if resource.is_empty() {
                    None
                } else if check_resource(&resource) {
                    Some(resource.to_string())
                } else {
                    return Err(ParseError::InvalidResourcePath);
                }
            }
            None => None,
        };

        Ok(BootUri { path, resource })
    }

    pub fn path(&self) -> &str {
        &self.path
    }

    pub fn resource(&self) -> Option<&str> {
        self.resource.as_ref().map(|s| s.as_str())
    }

    pub fn root_uri(&self) -> BootUri {
        BootUri { path: self.path.clone(), resource: None }
    }

    pub fn new_path(path: String) -> Result<BootUri, ParseError> {
        Ok(BootUri { path: path.clone(), resource: None })
    }

    pub fn new_resource(path: String, resource: String) -> Result<BootUri, ParseError> {
        let mut uri = BootUri::new_path(path)?;
        if resource.is_empty() || !check_resource(&resource) {
            return Err(ParseError::InvalidResourcePath);
        }
        uri.resource = Some(resource);
        Ok(uri)
    }
}

impl fmt::Display for BootUri {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "fuchsia-boot://{}", self.path)?;
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

#[cfg(test)]
mod tests {
    use super::*;

    macro_rules! test_parse_ok {
        (
            $(
                $test_name:ident => {
                    uri = $pkg_uri:expr,
                    path = $pkg_path:expr,
                    resource = $pkg_resource:expr,
                }
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    let pkg_uri = $pkg_uri.to_string();
                    assert_eq!(
                        BootUri::parse(&pkg_uri),
                        Ok(BootUri {
                            path: $pkg_path,
                            resource: $pkg_resource,
                        })
                    );
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
                            BootUri::parse(uri),
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
        test_parse_absolute_path => {
            uri = "fuchsia-boot:///package",
            path = "/package".to_string(),
            resource = None,
        }
        test_parse_root => {
            uri = "fuchsia-boot:///",
            path = "/".to_string(),
            resource = None,
        }
        test_parse_empty_root => {
            uri = "fuchsia-boot://",
            path = "/".to_string(),
            resource = None,
        }
        test_parse_resource => {
            uri = "fuchsia-boot:///package#resource",
            path = "/package".to_string(),
            resource = Some("resource".to_string()),
        }
        test_parse_empty_resource => {
            uri = "fuchsia-boot:///package#",
            path = "/package".to_string(),
            resource = None,
        }
        test_parse_root_empty_resource => {
            uri = "fuchsia-boot:///#",
            path = "/".to_string(),
            resource = None,
        }
        test_parse_empty_root_empty_resource => {
            uri = "fuchsia-boot://#",
            path = "/".to_string(),
            resource = None,
        }
    }

    test_parse_err! {
        test_parse_invalid_scheme => {
            uris = [
                "fuchsia-pkg://",
            ],
            err = ParseError::InvalidScheme,
        }
        test_parse_invalid_path => {
            uris = [
                "fuchsia-boot:////",
                "fuchsia-boot:///package:1234",
            ],
            err = ParseError::InvalidPath,
        }
        test_parse_extra_path => {
            uris = [
                "fuchsia-boot:///path/to",
            ],
            err = ParseError::ExtraPathSegments,
        }
        test_parse_path_cannot_be_longer_than_100_chars => {
            uris = [
                "fuchsia-boot:///12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901/",
            ],
            err = ParseError::InvalidPath,
        }
        test_parse_path_cannot_have_invalid_characters => {
            uris = [
                "fuchsia-boot:///$",
                "fuchsia-boot:///foo$bar",
            ],
            err = ParseError::InvalidPath,
        }
        test_parse_host_must_be_empty => {
            uris = [
                "fuchsia-boot://hello",
                "fuchsia-boot://user@fuchsia.com",
                "fuchsia-boot://user:password@fuchsia.com",
                "fuchsia-boot://:password@fuchsia.com",
            ],
            err = ParseError::HostMustBeEmpty,
        }
        test_parse_resource_cannot_be_slash => {
            uris = [
                "fuchsia-boot:///package#/",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_start_with_slash => {
            uris = [
                "fuchsia-boot:///package#/foo",
                "fuchsia-boot:///package#/foo/bar",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_end_with_slash => {
            uris = [
                "fuchsia-boot:///package#foo/",
                "fuchsia-boot:///package#foo/bar/",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_contain_dot_dot => {
            uris = [
                "fuchsia-boot:///package#foo/../bar",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_contain_empty_segments => {
            uris = [
                "fuchsia-boot:///package#foo//bar",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_contain_percent_encoded_nul_chars => {
            uris = [
                "fuchsia-boot:///package#foo%00bar",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_rejects_query_params => {
            uris = [
                "fuchsia-boot:///package?foo=bar",
            ],
            err = ParseError::CannotContainQueryParameters,
        }
    }

    test_format! {
        test_format_path_uri => {
            parsed = BootUri::new_path("/path/to".to_string()).unwrap(),
            formatted = "fuchsia-boot:///path/to",
        }
        test_format_resource_uri => {
            parsed = BootUri::new_resource("/path/to".to_string(), "path/to/resource".to_string()).unwrap(),
            formatted = "fuchsia-boot:///path/to#path/to/resource",
        }
    }

    #[test]
    fn test_new_path() {
        let uri = BootUri::new_path("/path/to".to_string()).unwrap();
        assert_eq!("/path/to", uri.path());
        assert_eq!(None, uri.resource());
        assert_eq!(uri, uri.root_uri());
        assert_eq!("fuchsia-boot:///path/to", format!("{}", uri.root_uri()));
    }

    #[test]
    fn test_new_resource() {
        let uri =
            BootUri::new_resource("/path/to".to_string(), "foo/bar".to_string()).unwrap();
        assert_eq!("/path/to", uri.path());
        assert_eq!(Some("foo/bar"), uri.resource());
        let mut uri_no_resource = uri.clone();
        uri_no_resource.resource = None;
        assert_eq!(uri_no_resource, uri.root_uri());
        assert_eq!("fuchsia-boot:///path/to", format!("{}", uri.root_uri()));
    }
}
