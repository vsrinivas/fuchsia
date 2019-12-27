// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use crate::errors::ParseError;
pub use crate::parse::{check_resource, is_name};
use percent_encoding::percent_decode;
use std::fmt;
use url::Url;

/// Decoded representation of a fuchsia-boot URL.
///
/// fuchsia-boot:///path/to#path/to/resource
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct BootUrl {
    path: String,
    resource: Option<String>,
}

impl BootUrl {
    pub fn parse(input: &str) -> Result<Self, ParseError> {
        let url = Url::parse(input)?;

        let scheme = url.scheme();
        if scheme != "fuchsia-boot" {
            return Err(ParseError::InvalidScheme);
        }

        let host = url.host().map(|h| h.to_string()).ok_or(ParseError::InvalidHost)?;
        if !host.is_empty() {
            return Err(ParseError::HostMustBeEmpty);
        }

        let path = url.path().to_string();

        // Validate that all path segments are valid.
        // Since host must be empty, first character of path must be '/'. Trim it.
        let check_path = &path[1..];
        if !check_path.is_empty() {
            let mut iter = check_path.split('/');
            while let Some(s) = iter.next() {
                if !is_name(s) {
                    return Err(ParseError::InvalidPath);
                }
            }
        }

        if url.query().is_some() {
            return Err(ParseError::CannotContainQueryParameters);
        }

        let resource = match url.fragment() {
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

        Ok(BootUrl { path, resource })
    }

    pub fn path(&self) -> &str {
        &self.path
    }

    pub fn resource(&self) -> Option<&str> {
        self.resource.as_ref().map(|s| s.as_str())
    }

    pub fn root_url(&self) -> BootUrl {
        BootUrl { path: self.path.clone(), resource: None }
    }

    pub fn new_path(path: String) -> Result<BootUrl, ParseError> {
        Ok(BootUrl { path: path.clone(), resource: None })
    }

    pub fn new_resource(path: String, resource: String) -> Result<BootUrl, ParseError> {
        let mut url = BootUrl::new_path(path)?;
        if resource.is_empty() || !check_resource(&resource) {
            return Err(ParseError::InvalidResourcePath);
        }
        url.resource = Some(resource);
        Ok(url)
    }
}

impl fmt::Display for BootUrl {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "fuchsia-boot://{}", self.path)?;
        if let Some(ref resource) = self.resource {
            write!(f, "#{}", percent_encoding::utf8_percent_encode(resource, crate::FRAGMENT))?;
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
                    url = $pkg_url:expr,
                    path = $pkg_path:expr,
                    resource = $pkg_resource:expr,
                }
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    let pkg_url = $pkg_url.to_string();
                    assert_eq!(
                        BootUrl::parse(&pkg_url),
                        Ok(BootUrl {
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
                            BootUrl::parse(url),
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
            url = "fuchsia-boot:///package",
            path = "/package".to_string(),
            resource = None,
        }
        test_parse_multiple_path_segments => {
            url = "fuchsia-boot:///package/foo",
            path = "/package/foo".to_string(),
            resource = None,
        }
        test_parse_more_path_segments => {
            url = "fuchsia-boot:///package/foo/bar/baz",
            path = "/package/foo/bar/baz".to_string(),
            resource = None,
        }
        test_parse_root => {
            url = "fuchsia-boot:///",
            path = "/".to_string(),
            resource = None,
        }
        test_parse_empty_root => {
            url = "fuchsia-boot://",
            path = "/".to_string(),
            resource = None,
        }
        test_parse_resource => {
            url = "fuchsia-boot:///package#resource",
            path = "/package".to_string(),
            resource = Some("resource".to_string()),
        }
        test_parse_resource_with_path_segments => {
            url = "fuchsia-boot:///package/foo#resource",
            path = "/package/foo".to_string(),
            resource = Some("resource".to_string()),
        }
        test_parse_empty_resource => {
            url = "fuchsia-boot:///package#",
            path = "/package".to_string(),
            resource = None,
        }
        test_parse_root_empty_resource => {
            url = "fuchsia-boot:///#",
            path = "/".to_string(),
            resource = None,
        }
        test_parse_empty_root_empty_resource => {
            url = "fuchsia-boot://#",
            path = "/".to_string(),
            resource = None,
        }
    }

    test_parse_err! {
        test_parse_invalid_scheme => {
            urls = [
                "fuchsia-pkg://",
            ],
            err = ParseError::InvalidScheme,
        }
        test_parse_invalid_path => {
            urls = [
                "fuchsia-boot:////",
                "fuchsia-boot:///package:1234",
            ],
            err = ParseError::InvalidPath,
        }
        test_parse_invalid_path_segment => {
            urls = [
                "fuchsia-boot:///path/foo$bar/baz",
            ],
            err = ParseError::InvalidPath,
        }
        test_parse_path_cannot_be_longer_than_100_chars => {
            urls = [
                "fuchsia-boot:///12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901/",
            ],
            err = ParseError::InvalidPath,
        }
        test_parse_path_cannot_have_invalid_characters => {
            urls = [
                "fuchsia-boot:///$",
                "fuchsia-boot:///foo$bar",
            ],
            err = ParseError::InvalidPath,
        }
        test_parse_host_must_be_empty => {
            urls = [
                "fuchsia-boot://hello",
                "fuchsia-boot://user@fuchsia.com",
                "fuchsia-boot://user:password@fuchsia.com",
                "fuchsia-boot://:password@fuchsia.com",
            ],
            err = ParseError::HostMustBeEmpty,
        }
        test_parse_resource_cannot_be_slash => {
            urls = [
                "fuchsia-boot:///package#/",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_start_with_slash => {
            urls = [
                "fuchsia-boot:///package#/foo",
                "fuchsia-boot:///package#/foo/bar",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_end_with_slash => {
            urls = [
                "fuchsia-boot:///package#foo/",
                "fuchsia-boot:///package#foo/bar/",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_contain_dot_dot => {
            urls = [
                "fuchsia-boot:///package#foo/../bar",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_contain_empty_segments => {
            urls = [
                "fuchsia-boot:///package#foo//bar",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_resource_cannot_contain_percent_encoded_nul_chars => {
            urls = [
                "fuchsia-boot:///package#foo%00bar",
            ],
            err = ParseError::InvalidResourcePath,
        }
        test_parse_rejects_query_params => {
            urls = [
                "fuchsia-boot:///package?foo=bar",
            ],
            err = ParseError::CannotContainQueryParameters,
        }
    }

    test_format! {
        test_format_path_url => {
            parsed = BootUrl::new_path("/path/to".to_string()).unwrap(),
            formatted = "fuchsia-boot:///path/to",
        }
        test_format_resource_url => {
            parsed = BootUrl::new_resource("/path/to".to_string(), "path/to/resource".to_string()).unwrap(),
            formatted = "fuchsia-boot:///path/to#path/to/resource",
        }
    }

    #[test]
    fn test_new_path() {
        let url = BootUrl::new_path("/path/to".to_string()).unwrap();
        assert_eq!("/path/to", url.path());
        assert_eq!(None, url.resource());
        assert_eq!(url, url.root_url());
        assert_eq!("fuchsia-boot:///path/to", format!("{}", url.root_url()));
    }

    #[test]
    fn test_new_resource() {
        let url = BootUrl::new_resource("/path/to".to_string(), "foo/bar".to_string()).unwrap();
        assert_eq!("/path/to", url.path());
        assert_eq!(Some("foo/bar"), url.resource());
        let mut url_no_resource = url.clone();
        url_no_resource.resource = None;
        assert_eq!(url_no_resource, url.root_url());
        assert_eq!("fuchsia-boot:///path/to", format!("{}", url.root_url()));
    }
}
