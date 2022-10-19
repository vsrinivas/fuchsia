// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use fuchsia_hash::{Hash, HASH_SIZE};

mod absolute_component_url;
mod absolute_package_url;
pub mod boot_url;
mod component_url;
pub mod errors;
mod host;
mod package_url;
mod parse;
mod pinned_absolute_package_url;
mod relative_component_url;
mod relative_package_url;
mod repository_url;
pub mod test;
mod unpinned_absolute_package_url;

pub use crate::{
    absolute_component_url::AbsoluteComponentUrl,
    absolute_package_url::AbsolutePackageUrl,
    component_url::ComponentUrl,
    errors::ParseError,
    package_url::PackageUrl,
    parse::{validate_resource_path, PackageName, PackageVariant, MAX_PACKAGE_PATH_SEGMENT_BYTES},
    pinned_absolute_package_url::PinnedAbsolutePackageUrl,
    relative_component_url::RelativeComponentUrl,
    relative_package_url::RelativePackageUrl,
    repository_url::RepositoryUrl,
    unpinned_absolute_package_url::UnpinnedAbsolutePackageUrl,
};

use {
    crate::host::Host,
    percent_encoding::{AsciiSet, CONTROLS},
};

/// https://url.spec.whatwg.org/#fragment-percent-encode-set
const FRAGMENT: &AsciiSet = &CONTROLS.add(b' ').add(b'"').add(b'<').add(b'>').add(b'`');

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Scheme {
    FuchsiaPkg,
    FuchsiaBoot,
}

#[derive(Debug, PartialEq, Eq)]
struct UrlParts {
    scheme: Option<Scheme>,
    host: Option<Host>,
    // a forward slash followed by zero or more validated path segments separated by forward slashes
    path: String,
    hash: Option<Hash>,
    // if present, String is a validated resource path
    resource: Option<String>,
}

impl UrlParts {
    fn parse(input: &str) -> Result<Self, ParseError> {
        let (scheme, url) = match url::Url::parse(input) {
            Ok(url) => (
                Some(match url.scheme() {
                    "fuchsia-pkg" => Scheme::FuchsiaPkg,
                    "fuchsia-boot" => Scheme::FuchsiaBoot,
                    _ => return Err(ParseError::InvalidScheme),
                }),
                url,
            ),
            Err(url::ParseError::RelativeUrlWithoutBase) => {
                (None, url::Url::parse("relative://")?.join(input)?)
            }
            Err(e) => Err(e)?,
        };

        if url.port().is_some() {
            return Err(ParseError::CannotContainPort);
        }

        if !url.username().is_empty() {
            return Err(ParseError::CannotContainUsername);
        }

        if url.password().is_some() {
            return Err(ParseError::CannotContainPassword);
        }

        let host = url
            .host_str()
            .filter(|s| !s.is_empty())
            .map(|s| Host::parse(s.to_string()))
            .transpose()?;

        let () = validate_path(url.path())?;
        let path = url.path().to_string();

        let hash = parse_query_pairs(url.query_pairs())?;

        let resource = if let Some(resource) = url.fragment() {
            let resource = percent_encoding::percent_decode(resource.as_bytes())
                .decode_utf8()
                .map_err(ParseError::ResourcePathPercentDecode)?;

            if resource.is_empty() {
                None
            } else {
                let () =
                    validate_resource_path(&resource).map_err(ParseError::InvalidResourcePath)?;
                Some(resource.to_string())
            }
        } else {
            None
        };

        Ok(Self { scheme, host, path, hash, resource })
    }
}

fn parse_query_pairs(pairs: url::form_urlencoded::Parse<'_>) -> Result<Option<Hash>, ParseError> {
    let mut query_hash = None;
    for (key, value) in pairs {
        if key == "hash" {
            if query_hash.is_some() {
                return Err(ParseError::MultipleHashes);
            }
            query_hash = Some(value.parse().map_err(ParseError::InvalidHash)?);
            // fuchsia-pkg URLs require lowercase hex characters, but fuchsia_hash::Hash::parse
            // accepts uppercase A-F.
            if !value.bytes().all(|b| (b >= b'0' && b <= b'9') || (b >= b'a' && b <= b'f')) {
                return Err(ParseError::UpperCaseHash);
            }
        } else {
            return Err(ParseError::ExtraQueryParameters);
        }
    }
    Ok(query_hash)
}

// Validates path is a forward slash followed by zero or more valid path segments separated by slash
fn validate_path(path: &str) -> Result<(), ParseError> {
    if let Some(suffix) = path.strip_prefix('/') {
        if !suffix.is_empty() {
            for s in suffix.split('/') {
                let () = crate::parse::validate_package_path_segment(s)
                    .map_err(ParseError::InvalidPathSegment)?;
            }
        }
        Ok(())
    } else {
        Err(ParseError::PathMustHaveLeadingSlash)
    }
}

// Validates that `path` is "/name[/variant]" and returns the name and optional variant if so.
fn parse_path_to_name_and_variant(
    path: &str,
) -> Result<(PackageName, Option<PackageVariant>), ParseError> {
    let path = path.strip_prefix('/').ok_or(ParseError::PathMustHaveLeadingSlash)?;
    if path.is_empty() {
        return Err(ParseError::MissingName);
    }
    let mut iter = path.split('/').fuse();
    let name = if let Some(s) = iter.next() {
        s.parse().map_err(ParseError::InvalidName)?
    } else {
        return Err(ParseError::MissingName);
    };
    let variant = if let Some(s) = iter.next() {
        Some(s.parse().map_err(ParseError::InvalidVariant)?)
    } else {
        None
    };
    if let Some(_) = iter.next() {
        return Err(ParseError::ExtraPathSegments);
    }
    Ok((name, variant))
}

#[cfg(test)]
mod test_validate_path {
    use {super::*, assert_matches::assert_matches};

    macro_rules! test_err {
        (
            $(
                $test_name:ident => {
                    path = $path:expr,
                    err = $err:pat,
                }
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    assert_matches!(
                        validate_path($path),
                        Err($err)
                    );
                }
            )+
        }
    }

    test_err! {
        err_no_leading_slash => {
            path = "just-name",
            err = ParseError::PathMustHaveLeadingSlash,
        }
        err_trailing_slash => {
            path = "/name/",
            err = ParseError::InvalidPathSegment(_),
        }
        err_empty_segment => {
            path = "/name//trailing",
            err = ParseError::InvalidPathSegment(_),
        }
        err_invalid_segment => {
            path = "/name/#/trailing",
            err = ParseError::InvalidPathSegment(_),
        }
    }

    #[test]
    fn success() {
        for path in ["/", "/name", "/name/other", "/name/other/more"] {
            let () = validate_path(path).unwrap();
        }
    }
}

#[cfg(test)]
mod test_parse_path_to_name_and_variant {
    use {super::*, assert_matches::assert_matches};

    macro_rules! test_err {
        (
            $(
                $test_name:ident => {
                    path = $path:expr,
                    err = $err:pat,
                }
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    assert_matches!(
                        parse_path_to_name_and_variant($path),
                        Err($err)
                    );
                }
            )+
        }
    }

    test_err! {
        err_no_leading_slash => {
            path = "just-name",
            err = ParseError::PathMustHaveLeadingSlash,
        }
        err_no_name => {
            path = "/",
            err = ParseError::MissingName,
        }
        err_empty_variant => {
            path = "/name/",
            err = ParseError::InvalidVariant(_),
        }
        err_trailing_slash => {
            path = "/name/variant/",
            err = ParseError::ExtraPathSegments,
        }
        err_extra_segment => {
            path = "/name/variant/extra",
            err = ParseError::ExtraPathSegments,
        }
        err_invalid_segment => {
            path = "/name/#",
            err = ParseError::InvalidVariant(_),
        }
    }

    #[test]
    fn success() {
        assert_eq!(
            ("name".parse().unwrap(), None),
            parse_path_to_name_and_variant("/name").unwrap()
        );
        assert_eq!(
            ("name".parse().unwrap(), Some("variant".parse().unwrap())),
            parse_path_to_name_and_variant("/name/variant").unwrap()
        );
    }
}

#[cfg(test)]
mod test_url_parts {
    use {super::*, crate::errors::ResourcePathError, assert_matches::assert_matches};

    macro_rules! test_parse_err {
        (
            $(
                $test_name:ident => {
                    url = $url:expr,
                    err = $err:pat,
                }
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    assert_matches!(
                        UrlParts::parse($url),
                        Err($err)
                    );
                }
            )+
        }
    }

    test_parse_err! {
        err_invalid_scheme => {
            url = "bad-scheme://example.org",
            err = ParseError::InvalidScheme,
        }
        err_port => {
            url = "fuchsia-pkg://example.org:1",
            err = ParseError::CannotContainPort,
        }
        err_username => {
            url = "fuchsia-pkg://user@example.org",
            err = ParseError::CannotContainUsername,
        }
        err_password => {
            url = "fuchsia-pkg://:password@example.org",
            err = ParseError::CannotContainPassword,
        }
        err_invalid_host => {
            url = "fuchsia-pkg://exa$mple.org",
            err = ParseError::InvalidHost,
        }
        // Path validation covered by test_validate_path, this just checks that the path is
        // validated at all.
        err_invalid_path => {
            url = "fuchsia-pkg://example.org//",
            err = ParseError::InvalidPathSegment(_),
        }
        err_empty_hash => {
            url = "fuchsia-pkg://example.org/?hash=",
            err = ParseError::InvalidHash(_),
        }
        err_invalid_hash => {
            url = "fuchsia-pkg://example.org/?hash=INVALID_HASH",
            err = ParseError::InvalidHash(_),
        }
        err_uppercase_hash => {
            url = "fuchsia-pkg://example.org/?hash=A000000000000000000000000000000000000000000000000000000000000000",
            err = ParseError::UpperCaseHash,
        }
        err_hash_too_long => {
            url = "fuchsia-pkg://example.org/?hash=00000000000000000000000000000000000000000000000000000000000000001",
            err = ParseError::InvalidHash(_),
        }
        err_hash_too_short => {
            url = "fuchsia-pkg://example.org/?hash=000000000000000000000000000000000000000000000000000000000000000",
            err = ParseError::InvalidHash(_),
        }
        err_multiple_hashes => {
            url = "fuchsia-pkg://example.org/?hash=0000000000000000000000000000000000000000000000000000000000000000&\
            hash=0000000000000000000000000000000000000000000000000000000000000000",
            err = ParseError::MultipleHashes,
        }
        err_non_hash_query_parameter => {
            url = "fuchsia-pkg://example.org/?invalid-key=invalid-value",
            err = ParseError::ExtraQueryParameters,
        }
        err_resource_slash => {
            url = "fuchsia-pkg://example.org/name#/",
            err = ParseError::InvalidResourcePath(ResourcePathError::PathStartsWithSlash),
        }
        err_resource_leading_slash => {
            url = "fuchsia-pkg://example.org/name#/resource",
            err = ParseError::InvalidResourcePath(ResourcePathError::PathStartsWithSlash),
        }
        err_resource_trailing_slash => {
            url = "fuchsia-pkg://example.org/name#resource/",
            err = ParseError::InvalidResourcePath(ResourcePathError::PathEndsWithSlash),
        }
        err_resource_empty_segment => {
            url = "fuchsia-pkg://example.org/name#resource//other",
            err = ParseError::InvalidResourcePath(ResourcePathError::NameEmpty),
        }
        err_resource_bad_segment => {
            url = "fuchsia-pkg://example.org/name#resource/./other",
            err = ParseError::InvalidResourcePath(ResourcePathError::NameIsDot),
        }
        err_resource_percent_encoded_null => {
            url = "fuchsia-pkg://example.org/name#resource%00",
            err = ParseError::InvalidResourcePath(ResourcePathError::NameContainsNull),
        }
    }

    macro_rules! test_parse_ok {
        (
            $(
                $test_name:ident => {
                    url = $url:expr,
                    scheme = $scheme:expr,
                    host = $host:expr,
                    path = $path:expr,
                    hash = $hash:expr,
                    resource = $resource:expr,
                }
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    assert_eq!(
                        UrlParts::parse($url).unwrap(),
                        UrlParts {
                            scheme: $scheme,
                            host: $host,
                            path: $path.into(),
                            hash: $hash,
                            resource: $resource,
                        }
                    )
                }
            )+
        }
    }

    test_parse_ok! {
        ok_fuchsia_pkg_scheme => {
            url =  "fuchsia-pkg://",
            scheme = Some(Scheme::FuchsiaPkg),
            host = None,
            path = "/",
            hash = None,
            resource = None,
        }
        ok_fuchsia_boot_scheme => {
            url =  "fuchsia-boot://",
            scheme = Some(Scheme::FuchsiaBoot),
            host = None,
            path = "/",
            hash = None,
            resource = None,
        }
        ok_host => {
            url =  "fuchsia-pkg://example.org",
            scheme = Some(Scheme::FuchsiaPkg),
            host = Some(Host::parse("example.org".into()).unwrap()),
            path = "/",
            hash = None,
            resource = None,
        }
        ok_path_single_segment => {
            url =  "fuchsia-pkg:///name",
            scheme = Some(Scheme::FuchsiaPkg),
            host = None,
            path = "/name",
            hash = None,
            resource = None,
        }
        ok_path_multiple_segment => {
            url =  "fuchsia-pkg:///name/variant/other",
            scheme = Some(Scheme::FuchsiaPkg),
            host = None,
            path = "/name/variant/other",
            hash = None,
            resource = None,
        }
        ok_hash => {
            url =  "fuchsia-pkg://?hash=0000000000000000000000000000000000000000000000000000000000000000",
            scheme = Some(Scheme::FuchsiaPkg),
            host = None,
            path = "/",
            hash = Some(
                "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap()
            ),
            resource = None,
        }
        ok_resource_single_segment => {
            url =  "fuchsia-pkg://#resource",
            scheme = Some(Scheme::FuchsiaPkg),
            host = None,
            path = "/",
            hash = None,
            resource = Some("resource".into()),
        }
        ok_resource_multiple_segment => {
            url =  "fuchsia-pkg://#resource/again/third",
            scheme = Some(Scheme::FuchsiaPkg),
            host = None,
            path = "/",
            hash = None,
            resource = Some("resource/again/third".into()),
        }
        ok_resource_ignores_null => {
            url =  "fuchsia-pkg://#reso\x00urce",
            scheme = Some(Scheme::FuchsiaPkg),
            host = None,
            path = "/",
            hash = None,
            resource = Some("resource".into()),
        }
        ok_resource_encoded_control_character => {
            url =  "fuchsia-pkg://#reso%09urce",
            scheme = Some(Scheme::FuchsiaPkg),
            host = None,
            path = "/",
            hash = None,
            resource = Some("reso\turce".into()),
        }
        ok_all_fields => {
            url =  "fuchsia-pkg://example.org/name\
            ?hash=0000000000000000000000000000000000000000000000000000000000000000\
            #resource",
            scheme = Some(Scheme::FuchsiaPkg),
            host = Some(Host::parse("example.org".into()).unwrap()),
            path = "/name",
            hash = Some(
                "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap()
            ),
            resource = Some("resource".into()),
        }
        ok_relative_path_single_segment => {
            url =  "name",
            scheme = None,
            host = None,
            path = "/name",
            hash = None,
            resource = None,
        }
        ok_relative_path_single_segment_leading_slash => {
            url =  "/name",
            scheme = None,
            host = None,
            path = "/name",
            hash = None,
            resource = None,
        }
        ok_relative_path_multiple_segment => {
            url =  "name/variant/other",
            scheme = None,
            host = None,
            path = "/name/variant/other",
            hash = None,
            resource = None,
        }
        ok_relative_path_multiple_segment_leading_slash => {
            url =  "/name/variant/other",
            scheme = None,
            host = None,
            path = "/name/variant/other",
            hash = None,
            resource = None,
        }
        ok_relative_hash => {
            url =  "?hash=0000000000000000000000000000000000000000000000000000000000000000",
            scheme = None,
            host = None,
            path = "/",
            hash = Some(
                "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap()
            ),
            resource = None,
        }
        ok_relative_resource_single_segment => {
            url =  "#resource",
            scheme = None,
            host = None,
            path = "/",
            hash = None,
            resource = Some("resource".into()),
        }
        ok_relative_resource_multiple_segment => {
            url =  "#resource/again/third",
            scheme = None,
            host = None,
            path = "/",
            hash = None,
            resource = Some("resource/again/third".into()),
        }
        ok_relative_all_fields => {
            url =  "name\
            ?hash=0000000000000000000000000000000000000000000000000000000000000000\
            #resource",
            scheme = None,
            host = None,
            path = "/name",
            hash = Some(
                "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap()
            ),
            resource = Some("resource".into()),
        }
    }
}
