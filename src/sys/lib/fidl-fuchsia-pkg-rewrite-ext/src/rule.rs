// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::errors::{RuleDecodeError, RuleParseError},
    fidl_fuchsia_pkg_rewrite as fidl,
    fuchsia_url::{AbsolutePackageUrl, ParseError, RepositoryUrl},
    serde::{Deserialize, Serialize},
    std::convert::TryFrom,
};

/// A `Rule` can be used to re-write parts of a [`AbsolutePackageUrl`].
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct Rule {
    host_match: RepositoryUrl,
    host_replacement: RepositoryUrl,
    path_prefix_match: String,
    path_prefix_replacement: String,
}

/// Wraper for serializing rewrite rules to the on-disk JSON format.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
pub enum RuleConfig {
    #[allow(missing_docs)]
    #[serde(rename = "1")]
    Version1(Vec<Rule>),
}

impl Rule {
    /// Creates a new `Rule`.
    pub fn new(
        host_match: impl Into<String>,
        host_replacement: impl Into<String>,
        path_prefix_match: impl Into<String>,
        path_prefix_replacement: impl Into<String>,
    ) -> Result<Self, RuleParseError> {
        Self::new_impl(
            host_match.into(),
            host_replacement.into(),
            path_prefix_match.into(),
            path_prefix_replacement.into(),
        )
    }

    fn new_impl(
        host_match: String,
        host_replacement: String,
        path_prefix_match: String,
        path_prefix_replacement: String,
    ) -> Result<Self, RuleParseError> {
        let host_match =
            RepositoryUrl::parse_host(host_match).map_err(|_| RuleParseError::InvalidHost)?;
        let host_replacement =
            RepositoryUrl::parse_host(host_replacement).map_err(|_| RuleParseError::InvalidHost)?;

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

    /// The exact hostname to match.
    pub fn host_match(&self) -> &str {
        self.host_match.host()
    }

    /// The new hostname to replace the matched `host_match` with.
    pub fn host_replacement(&self) -> &str {
        self.host_replacement.host()
    }

    /// The absolute path to a package or directory to match against.
    pub fn path_prefix_match(&self) -> &str {
        &self.path_prefix_match
    }

    /// The absolute path to a single package or a directory to replace the
    /// matched `path_prefix_match` with.
    pub fn path_prefix_replacement(&self) -> &str {
        &self.path_prefix_replacement
    }

    /// Apply this `Rule` to the given [`AbsolutePackageUrl`].
    ///
    /// In order for a `Rule` to match a particular fuchsia-pkg:// URI, `host` must match `uri`'s
    /// host exactly and `path` must prefix match the `uri`'s path at a '/' boundary.  If `path`
    /// doesn't end in a '/', it must match the entire `uri` path.
    ///
    /// When a `Rule` does match the given `uri`, it will replace the matched hostname and path
    /// with the given replacement strings, preserving the unmatched part of the path, the hash
    /// query parameter, and any fragment.
    pub fn apply(
        &self,
        uri: &AbsolutePackageUrl,
    ) -> Option<Result<AbsolutePackageUrl, ParseError>> {
        if uri.host() != self.host_match.host() {
            return None;
        }

        let full_path = uri.path();
        let new_path = if self.path_prefix_match.ends_with('/') {
            let rest = full_path.strip_prefix(&self.path_prefix_match)?;

            format!("{}{}", self.path_prefix_replacement, rest)
        } else {
            if full_path != self.path_prefix_match {
                return None;
            }

            self.path_prefix_replacement.clone()
        };

        Some(AbsolutePackageUrl::new_with_path(
            self.host_replacement.clone(),
            &new_path,
            uri.hash(),
        ))
    }
}

impl TryFrom<fidl::Rule> for Rule {
    type Error = RuleDecodeError;
    fn try_from(rule: fidl::Rule) -> Result<Self, Self::Error> {
        let rule = match rule {
            fidl::Rule::Literal(rule) => rule,
            _ => return Err(RuleDecodeError::UnknownVariant),
        };

        Ok(Rule::new(
            rule.host_match,
            rule.host_replacement,
            rule.path_prefix_match,
            rule.path_prefix_replacement,
        )?)
    }
}

impl From<Rule> for fidl::Rule {
    fn from(rule: Rule) -> Self {
        fidl::Rule::Literal(fidl::LiteralRule {
            host_match: rule.host_match.into_host(),
            host_replacement: rule.host_replacement.into_host(),
            path_prefix_match: rule.path_prefix_match,
            path_prefix_replacement: rule.path_prefix_replacement,
        })
    }
}

impl serde::Serialize for Rule {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        #[derive(Serialize)]
        struct TempRule<'a> {
            host_match: &'a str,
            host_replacement: &'a str,
            path_prefix_match: &'a str,
            path_prefix_replacement: &'a str,
        }

        TempRule {
            host_match: self.host_match(),
            host_replacement: self.host_replacement(),
            path_prefix_match: &self.path_prefix_match,
            path_prefix_replacement: &self.path_prefix_replacement,
        }
        .serialize(serializer)
    }
}

impl<'de> serde::Deserialize<'de> for Rule {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        #[derive(Deserialize)]
        struct TempRule {
            host_match: String,
            host_replacement: String,
            path_prefix_match: String,
            path_prefix_replacement: String,
        }

        let t = TempRule::deserialize(deserializer)?;
        Rule::new(t.host_match, t.host_replacement, t.path_prefix_match, t.path_prefix_replacement)
            .map_err(|e| serde::de::Error::custom(e.to_string()))
    }
}

#[cfg(test)]
mod serde_tests {
    use super::*;

    use serde_json::json;

    macro_rules! rule {
        ($host_match:expr => $host_replacement:expr,
         $path_prefix_match:expr => $path_prefix_replacement:expr) => {
            Rule::new($host_match, $host_replacement, $path_prefix_match, $path_prefix_replacement)
                .unwrap()
        };
    }

    macro_rules! assert_error_contains {
        ($err:expr, $text:expr,) => {
            let error_message = $err.to_string();
            assert!(
                error_message.contains($text),
                r#"error message did not contain "{}", was actually "{}""#,
                $text,
                error_message
            );
        };
    }

    #[test]
    fn test_rejects_malformed_fidl() {
        let as_fidl = fidl::Rule::Literal(fidl::LiteralRule {
            host_match: "example.com".to_owned(),
            host_replacement: "example.com".to_owned(),
            path_prefix_match: "/test/".to_owned(),
            path_prefix_replacement: "/test".to_owned(),
        });
        assert_eq!(
            Rule::try_from(as_fidl),
            Err(RuleDecodeError::ParseError(RuleParseError::InconsistentPaths))
        );

        let as_fidl = fidl::Rule::Literal(fidl::LiteralRule {
            host_match: "example.com".to_owned(),
            host_replacement: "example.com".to_owned(),
            path_prefix_match: "/test".to_owned(),
            path_prefix_replacement: "test".to_owned(),
        });
        assert_eq!(
            Rule::try_from(as_fidl),
            Err(RuleDecodeError::ParseError(RuleParseError::InvalidPath))
        );
    }

    #[test]
    fn test_rejects_unknown_fidl_variant() {
        let as_fidl = fidl::Rule::unknown(0, Default::default());
        assert_eq!(Rule::try_from(as_fidl), Err(RuleDecodeError::UnknownVariant));
    }

    #[test]
    fn test_rejects_unknown_json_version() {
        let json = json!({
            "version": "9001",
            "content": "the future",
        });
        assert_error_contains!(
            serde_json::from_str::<RuleConfig>(json.to_string().as_str()).unwrap_err(),
            "unknown variant",
        );
    }

    #[test]
    fn test_rejects_malformed_json() {
        let json = json!({
            "version": "1",
            "content": [{
                "host_match":              "example.com",
                "host_replacement":        "example.com",
                "path_prefix_match":       "/test/",
                "path_prefix_replacement": "/test",
            }]
        });

        assert_error_contains!(
            serde_json::from_str::<Rule>(json["content"][0].to_string().as_str()).unwrap_err(),
            "paths should both be a prefix match or both be a literal match",
        );
        assert_error_contains!(
            serde_json::from_str::<RuleConfig>(json.to_string().as_str()).unwrap_err(),
            "paths should both be a prefix match or both be a literal match",
        );

        let json = json!({
            "version": "1",
            "content": [{
                "host_match":              "example.com",
                "host_replacement":        "example.com",
                "path_prefix_match":       "test",
                "path_prefix_replacement": "/test",
            }]
        });

        assert_error_contains!(
            serde_json::from_str::<Rule>(json["content"][0].to_string().as_str()).unwrap_err(),
            "paths must start with",
        );
        assert_error_contains!(
            serde_json::from_str::<RuleConfig>(json.to_string().as_str()).unwrap_err(),
            "paths must start with",
        );
    }

    #[test]
    fn test_parse_all_foo_to_bar_rules() {
        let json = json!({
            "version": "1",
            "content": [{
                "host_match":              "example.com",
                "host_replacement":        "example.com",
                "path_prefix_match":       "/foo",
                "path_prefix_replacement": "/bar",
            },{
                "host_match":              "example.com",
                "host_replacement":        "example.com",
                "path_prefix_match":       "/foo/",
                "path_prefix_replacement": "/bar/",
            }]
        });

        let expected = RuleConfig::Version1(vec![
            rule!("example.com" => "example.com", "/foo" => "/bar"),
            rule!("example.com" => "example.com", "/foo/" => "/bar/"),
        ]);

        assert_eq!(
            serde_json::from_str::<RuleConfig>(json.to_string().as_str()).unwrap(),
            expected
        );

        assert_eq!(serde_json::to_value(expected).unwrap(), json);
    }
}

#[cfg(test)]
mod rule_tests {
    use super::*;
    use assert_matches::assert_matches;

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
                        $host_match,
                        $host_replacement,
                        $path_prefix_match,
                        $path_prefix_replacement,
                    )
                    .expect_err("should have failed to parse");
                    assert_eq!(error, $error);

                    let error = Rule::new(
                        $host_replacement,
                        $host_match,
                        $path_prefix_replacement,
                        $path_prefix_match,
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
        test_err_invalid_host_match_uppercase => {
            host = "EXAMPLE.ORG" => "example.com",
            path = "/" => "/",
            error = RuleParseError::InvalidHost,
        }
        test_err_invalid_host_replacement_uppercase => {
            host = "example.org" => "EXAMPLE.COM",
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

    // Assumes apply creates a valid AbsolutePackageUrl if it matches
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
                        let input = AbsolutePackageUrl::parse($input).unwrap();
                        let output: Option<&str> = $output;
                        let output = output.map(|s| AbsolutePackageUrl::parse(s).unwrap());
                        assert_eq!(
                            rule.apply(&input).map(|res| res.unwrap()),
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
                "fuchsia-pkg://fuchsia.com/rolldice" => Some("fuchsia-pkg://fuchsia.com/rolldice"),
                "fuchsia-pkg://fuchsia.com/rolldice/0" => Some("fuchsia-pkg://fuchsia.com/rolldice/0"),
                "fuchsia-pkg://fuchsia.com/foo/0?hash=00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff" => Some(
                "fuchsia-pkg://fuchsia.com/foo/0?hash=00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"),

                "fuchsia-pkg://example.com/rolldice" => None,
                "fuchsia-pkg://example.com/rolldice/0" => None,
            ],
        }
        test_inject_subdomain => {
            host = "fuchsia.com" => "test.fuchsia.com",
            path = "/" => "/",
            cases = [
                "fuchsia-pkg://fuchsia.com/rolldice" => Some("fuchsia-pkg://test.fuchsia.com/rolldice"),
                "fuchsia-pkg://fuchsia.com/rolldice/0" => Some("fuchsia-pkg://test.fuchsia.com/rolldice/0"),

                "fuchsia-pkg://example.com/rolldice" => None,
                "fuchsia-pkg://example.com/rolldice/0" => None,
            ],
        }
        test_inject_subdir => {
            host = "fuchsia.com" => "fuchsia.com",
            path = "/foo" => "/foo/bar",
            cases = [
                "fuchsia-pkg://fuchsia.com/foo" => Some("fuchsia-pkg://fuchsia.com/foo/bar"),
                // TODO not supported until fuchsia-pkg URIs allow arbitrary package paths
                //"fuchsia-pkg://fuchsia.com/foo/0" => Some("fuchsia-pkg://fuchsia.com/foo/bar/0")),
            ],
        }
        test_inject_parent_dir => {
            host = "fuchsia.com" => "fuchsia.com",
            path = "/foo" => "/bar/foo",
            cases = [
                "fuchsia-pkg://fuchsia.com/foo" => Some("fuchsia-pkg://fuchsia.com/bar/foo"),
            ],
        }
        test_replace_host => {
            host = "fuchsia.com" => "example.com",
            path = "/" => "/",
            cases = [
                "fuchsia-pkg://fuchsia.com/rolldice" => Some("fuchsia-pkg://example.com/rolldice"),
                "fuchsia-pkg://fuchsia.com/rolldice/0" => Some("fuchsia-pkg://example.com/rolldice/0"),

                "fuchsia-pkg://example.com/rolldice" => None,
                "fuchsia-pkg://example.com/rolldice/0" => None,
            ],
        }
        test_replace_host_for_single_package => {
            host = "fuchsia.com" => "example.com",
            path = "/rolldice" => "/rolldice",
            cases = [
                "fuchsia-pkg://fuchsia.com/rolldice" => Some("fuchsia-pkg://example.com/rolldice"),

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
                "fuchsia-pkg://fuchsia.com/rolldice/0" => Some("fuchsia-pkg://example.com/rolldice/0"),
                "fuchsia-pkg://fuchsia.com/rolldice/stable" => Some("fuchsia-pkg://example.com/rolldice/stable"),

                // package with same name as directory doesn't match
                "fuchsia-pkg://fuchsia.com/rolldice" => None,
            ],
        }
        test_rename_package => {
            host = "fuchsia.com" => "fuchsia.com",
            path = "/fake" => "/real",
            cases = [
                "fuchsia-pkg://fuchsia.com/fake" => Some("fuchsia-pkg://fuchsia.com/real"),

                // not the same packages
                "fuchsia-pkg://fuchsia.com/fakeout" => None,
            ],
        }
        test_rename_directory => {
            host = "fuchsia.com" => "fuchsia.com",
            path = "/fake/" => "/real/",
            cases = [
                "fuchsia-pkg://fuchsia.com/fake/0" => Some("fuchsia-pkg://fuchsia.com/real/0"),
                "fuchsia-pkg://fuchsia.com/fake/package" => Some("fuchsia-pkg://fuchsia.com/real/package"),

                // a package called "fake", not a directory.
                "fuchsia-pkg://fuchsia.com/fake" => None,
            ],
        }
    }

    #[test]
    fn test_apply_creates_invalid_url() {
        let rule = Rule::new("fuchsia.com", "fuchsia.com", "/", "/a+b/").unwrap();
        assert_matches!(
            rule.apply(&"fuchsia-pkg://fuchsia.com/foo".parse().unwrap()),
            Some(Err(ParseError::InvalidName(_)))
        );
    }
}
