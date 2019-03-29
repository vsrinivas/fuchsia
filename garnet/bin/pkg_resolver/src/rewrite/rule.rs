// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//TODO remove once re-write rules are used by the resolver
#![allow(dead_code)]

use {
    failure::Fail,
    fuchsia_uri::pkg_uri::{FuchsiaPkgUri, ParseError},
};

/// A `Rule` can be used to re-write parts of a [`FuchsiaPkgUri`].
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Rule {
    host_match: String,
    host_replacement: String,
    path_prefix_match: String,
    path_prefix_replacement: String,
}

#[derive(Debug, Fail, Clone, PartialEq, Eq)]
pub enum RuleParseError {
    #[fail(display = "invalid hostname")]
    InvalidHost,

    #[fail(display = "paths must start with '/'")]
    InvalidPath,

    #[fail(display = "paths should both be a prefix match or both be a literal match")]
    InconsistentPaths,
}

impl Rule {
    /// Creates a new `Rule`.
    pub fn new(
        host_match: String,
        host_replacement: String,
        path_prefix_match: String,
        path_prefix_replacement: String,
    ) -> Result<Self, RuleParseError> {
        fn validate_host(s: &str) -> Result<(), RuleParseError> {
            FuchsiaPkgUri::new_repository(s.to_owned())
                .map_err(|_err| RuleParseError::InvalidHost)?;
            Ok(())
        }

        validate_host(host_match.as_str())?;
        validate_host(host_replacement.as_str())?;

        if !path_prefix_match.starts_with('/') {
            return Err(RuleParseError::InvalidPath);
        }
        if !path_prefix_replacement.starts_with('/') {
            return Err(RuleParseError::InvalidPath);
        }

        // Literal matches should have a literal replacement and prefix matches should have a
        // prefix replacement.
        if path_prefix_match.ends_with('/') != path_prefix_replacement.ends_with('/') {
            return Err(RuleParseError::InconsistentPaths);
        }

        Ok(Self { host_match, host_replacement, path_prefix_match, path_prefix_replacement })
    }

    /// Apply this `Rule` to the given [`FuchsiaPkgUri`].
    ///
    /// In order for a `Rule` to match a particular fuchsia-pkg:// URI, `host` must match `uri`'s
    /// host exactly and `path` must prefix match the `uri`'s path at a '/' boundary.  If `path`
    /// doesn't end in a '/', it must match the entire `uri` path.
    ///
    /// When a `Rule` does match the given `uri`, it will replace the matched hostname and path
    /// with the given replacement strings, preserving the unmatched part of the path, the hash
    /// query parameter, and any fragment.
    pub fn apply(&self, uri: &FuchsiaPkgUri) -> Option<Result<FuchsiaPkgUri, ParseError>> {
        if uri.host() != self.host_match {
            return None;
        }

        let full_path = uri.path();
        let new_path = if self.path_prefix_match.ends_with('/') {
            if !full_path.starts_with(self.path_prefix_match.as_str()) {
                return None;
            }

            let (_, rest) = full_path.split_at(self.path_prefix_match.len());

            format!("{}{}", self.path_prefix_replacement, rest)
        } else {
            if full_path != self.path_prefix_match {
                return None;
            }

            self.path_prefix_replacement.clone()
        };

        Some(match (new_path.as_str(), uri.resource()) {
            ("/", _) => FuchsiaPkgUri::new_repository(self.host_replacement.clone()),

            (_, None) => FuchsiaPkgUri::new_package(
                self.host_replacement.clone(),
                new_path,
                uri.hash().map(|s| s.to_owned()),
            ),

            (_, Some(resource)) => FuchsiaPkgUri::new_resource(
                self.host_replacement.clone(),
                new_path,
                uri.hash().map(|s| s.to_owned()),
                resource.to_owned(),
            ),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    macro_rules! test_new_error {
        (
            $(
                $test_name:ident => {
                    host = $host_match:expr => $host_replacement:expr,
                    path = $path_prefix_match:expr => $path_prefix_replacement:expr,
                    error = $error:expr,
                }
            )+
        ) => {
            $(

                #[test]
                fn $test_name() {
                    let error = Rule::new(
                        $host_match.to_owned(),
                        $host_replacement.to_owned(),
                        $path_prefix_match.to_owned(),
                        $path_prefix_replacement.to_owned()
                    )
                    .expect_err("should have failed to parse");
                    assert_eq!(error, $error);

                    let error = Rule::new(
                        $host_replacement.to_owned(),
                        $host_match.to_owned(),
                        $path_prefix_replacement.to_owned(),
                        $path_prefix_match.to_owned()
                    )
                    .expect_err("should have failed to parse");
                    assert_eq!(error, $error);
                }
            )+
        }
    }

    test_new_error! {
        test_err_empty_host => {
            host = "" => "example.com",
            path = "/" => "/",
            error = RuleParseError::InvalidHost,
        }
        test_err_empty_path => {
            host = "fuchsia.com" => "fuchsia.com",
            path = "" => "rolldice",
            error = RuleParseError::InvalidPath,
        }
        test_err_relative_path => {
            host = "example.com" => "example.com",
            path = "/rolldice" => "rolldice",
            error = RuleParseError::InvalidPath,
        }
        test_err_inconsistent_match_type => {
            host = "example.com" => "example.com",
            path = "/rolldice/" => "/fortune",
            error = RuleParseError::InconsistentPaths,
        }
    }

    macro_rules! test_apply {
        (
            $(
                $test_name:ident => {
                    host = $host_match:expr => $host_replacement:expr,
                    path = $path_prefix_match:expr => $path_prefix_replacement:expr,
                    cases = [ $(
                        $input:expr => $output:expr,
                    )+ ],
                }
            )+
        ) => {
            $(

                #[test]
                fn $test_name() {
                    let rule = Rule::new(
                        $host_match.to_owned(),
                        $host_replacement.to_owned(),
                        $path_prefix_match.to_owned(),
                        $path_prefix_replacement.to_owned()
                    )
                    .unwrap();

                    $(
                        let input = FuchsiaPkgUri::parse($input).unwrap();
                        let output: Option<Result<&str, ParseError>> = $output;
                        let output: Option<Result<FuchsiaPkgUri, _>> = match output {
                            Some(Ok(s)) => Some(Ok(FuchsiaPkgUri::parse(s).unwrap())),
                            Some(Err(x)) => Some(Err(x)),
                            None => None,
                        };
                        assert_eq!(
                            rule.apply(&input),
                            output,
                            "\n\nusing rule {:?}\nexpected {}\nto map to {},\nbut got {:?}\n\n",
                            rule,
                            $input,
                            stringify!($output),
                            rule.apply(&input).map(|x| x.map(|uri| uri.to_string())),
                        );
                    )+
                }
            )+
        }
    }

    test_apply! {
        test_nop => {
            host = "fuchsia.com" => "fuchsia.com",
            path = "/" => "/",
            cases = [
                "fuchsia-pkg://fuchsia.com" => Some(Ok("fuchsia-pkg://fuchsia.com")),
                "fuchsia-pkg://fuchsia.com/rolldice" => Some(Ok("fuchsia-pkg://fuchsia.com/rolldice")),
                "fuchsia-pkg://fuchsia.com/rolldice/0" => Some(Ok("fuchsia-pkg://fuchsia.com/rolldice/0")),
                "fuchsia-pkg://fuchsia.com/rolldice/0#meta/bin.cmx" => Some(Ok("fuchsia-pkg://fuchsia.com/rolldice/0#meta/bin.cmx")),
                "fuchsia-pkg://fuchsia.com/foo/0?hash=00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff" => Some(Ok(
                "fuchsia-pkg://fuchsia.com/foo/0?hash=00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff")),

                "fuchsia-pkg://example.com" => None,
                "fuchsia-pkg://example.com/rolldice" => None,
                "fuchsia-pkg://example.com/rolldice/0" => None,
            ],
        }
        test_inject_subdomain => {
            host = "fuchsia.com" => "test.fuchsia.com",
            path = "/" => "/",
            cases = [
                "fuchsia-pkg://fuchsia.com" => Some(Ok("fuchsia-pkg://test.fuchsia.com")),
                "fuchsia-pkg://fuchsia.com/rolldice" => Some(Ok("fuchsia-pkg://test.fuchsia.com/rolldice")),
                "fuchsia-pkg://fuchsia.com/rolldice/0" => Some(Ok("fuchsia-pkg://test.fuchsia.com/rolldice/0")),

                "fuchsia-pkg://example.com" => None,
                "fuchsia-pkg://example.com/rolldice" => None,
                "fuchsia-pkg://example.com/rolldice/0" => None,
            ],
        }
        test_inject_subdir => {
            host = "fuchsia.com" => "fuchsia.com",
            path = "/foo" => "/foo/bar",
            cases = [
                "fuchsia-pkg://fuchsia.com/foo" => Some(Ok("fuchsia-pkg://fuchsia.com/foo/bar")),
                // TODO not supported until fuchsia-pkg URIs allow arbitrary package paths
                //"fuchsia-pkg://fuchsia.com/foo/0" => Some(Ok("fuchsia-pkg://fuchsia.com/foo/bar/0")),
            ],
        }
        test_inject_parent_dir => {
            host = "fuchsia.com" => "fuchsia.com",
            path = "/foo" => "/bar/foo",
            cases = [
                "fuchsia-pkg://fuchsia.com/foo" => Some(Ok("fuchsia-pkg://fuchsia.com/bar/foo")),
            ],
        }
        test_replace_host => {
            host = "fuchsia.com" => "example.com",
            path = "/" => "/",
            cases = [
                "fuchsia-pkg://fuchsia.com" => Some(Ok("fuchsia-pkg://example.com")),
                "fuchsia-pkg://fuchsia.com/rolldice" => Some(Ok("fuchsia-pkg://example.com/rolldice")),
                "fuchsia-pkg://fuchsia.com/rolldice/0" => Some(Ok("fuchsia-pkg://example.com/rolldice/0")),

                "fuchsia-pkg://example.com" => None,
                "fuchsia-pkg://example.com/rolldice" => None,
                "fuchsia-pkg://example.com/rolldice/0" => None,
            ],
        }
        test_replace_host_for_single_package => {
            host = "fuchsia.com" => "example.com",
            path = "/rolldice" => "/rolldice",
            cases = [
                "fuchsia-pkg://fuchsia.com/rolldice" => Some(Ok("fuchsia-pkg://example.com/rolldice")),

                // this path pattern is a literal match
                "fuchsia-pkg://fuchsia.com/rolldicer" => None,

                // unrelated packages don't match
                "fuchsia-pkg://fuchsia.com/fortune" => None,
            ],
        }
        test_replace_host_for_package_prefix => {
            host = "fuchsia.com" => "example.com",
            path = "/rolldice/" => "/rolldice/",
            cases = [
                "fuchsia-pkg://fuchsia.com/rolldice/0" => Some(Ok("fuchsia-pkg://example.com/rolldice/0")),
                "fuchsia-pkg://fuchsia.com/rolldice/stable" => Some(Ok("fuchsia-pkg://example.com/rolldice/stable")),

                // package with same name as directory doesn't match
                "fuchsia-pkg://fuchsia.com/rolldice" => None,
            ],
        }
        test_rename_package => {
            host = "fuchsia.com" => "fuchsia.com",
            path = "/fake" => "/real",
            cases = [
                "fuchsia-pkg://fuchsia.com/fake" => Some(Ok("fuchsia-pkg://fuchsia.com/real")),

                // not the same packages
                "fuchsia-pkg://fuchsia.com/fakeout" => None,
            ],
        }
        test_rename_directory => {
            host = "fuchsia.com" => "fuchsia.com",
            path = "/fake/" => "/real/",
            cases = [
                "fuchsia-pkg://fuchsia.com/fake/0" => Some(Ok("fuchsia-pkg://fuchsia.com/real/0")),
                "fuchsia-pkg://fuchsia.com/fake/package" => Some(Ok("fuchsia-pkg://fuchsia.com/real/package")),

                // a package called "fake", not a directory.
                "fuchsia-pkg://fuchsia.com/fake" => None,
            ],
        }
        test_invalid_new_path => {
            host = "fuchsia.com" => "fuchsia.com",
            path = "/" => "/a+b/",
            cases = [
                "fuchsia-pkg://fuchsia.com" => Some(Err(ParseError::InvalidName)),
                "fuchsia-pkg://fuchsia.com/foo" => Some(Err(ParseError::InvalidName)),
            ],
        }
    }
}
