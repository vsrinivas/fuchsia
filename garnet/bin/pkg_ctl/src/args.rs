// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::Error, fidl_fuchsia_pkg_ext::BlobId, fuchsia_url_rewrite::RuleConfig, serde_json,
    std::path::PathBuf,
};

const HELP: &str = "\
USAGE:
    pkgctl <SUBCOMMAND>

FLAGS:
    -h, --help       prints help information

SUBCOMMANDS:
    help       prints this message or the help of the given subcommand(s)
    open       open a package by merkle root
    repo       repo subcommands
    resolve    resolve a package
    rule       manage URL rewrite rules
";

const HELP_OPEN: &str = "\
open a package by merkle root

USAGE:
    pkgctl open <meta_far_blob_id> [selectors]...

ARGS:
    <meta_far_blob_id>    Merkle root of package's meta.far to cache
    <selectors>...        Package selectors
";

const HELP_REPO: &str = "\
repo subcommands

USAGE:
    pkgctl repo <SUBCOMMAND>

SUBCOMMANDS:
    add       add a repository
    help      prints this message or the help of the given subcommand(s)
    list      list repositories
    remove    remove a repository
";

const HELP_REPO_ADD: &str = "\
add a repository

USAGE:
    pkgctl repo add <file>

OPTIONS:
    -f, --file <file>    path to a repository config file
";

const HELP_REPO_LIST: &str = "\
list repositories

USAGE:
    pkgctl repo list
";

const HELP_REPO_REMOVE: &str = "\
remove a repository

USAGE:
    pkgctl repo remove --repo-url <repo_url>

OPTIONS:
        --repo-url <repo_url>    the repository url to remove
";

const HELP_RESOLVE: &str = "\
resolve a package

USAGE:
    pkgctl resolve <pkg_url> [selectors]...

ARGS:
    <pkg_url>         URL of package to cache
    <selectors>...    Package selectors
";

const HELP_RULE: &str = "\
manage URL rewrite rules

USAGE:
    pkgctl rule <SUBCOMMAND>

SUBCOMMANDS:
    clear      clear all rules
    help       prints this message or the help of the given subcommand(s)
    list       list all rules
    replace    replace all dynamic rules with the provided rules
";

const HELP_RULE_CLEAR: &str = "\
clear all rules

USAGE:
    pkgctl rule clear
";

const HELP_RULE_LIST: &str = "\
list all rules

USAGE:
    pkgctl rule list
";

const HELP_RULE_REPLACE: &str = "\
replace all dynamic rules with the provided rules

USAGE:
    pkgctl rule replace <SUBCOMMAND>

SUBCOMMANDS:
    file
    json
";

const HELP_RULE_REPLACE_FILE: &str = "\
USAGE:
    pkgctl rule replace file <path>

ARGS:
    <path>    path to rewrite rule config file
";

const HELP_RULE_REPLACE_JSON: &str = "\
USAGE:
    pkgctl rule replace json <config>

ARGS:
    <config>    JSON encoded rewrite rule config
";

#[derive(Debug, PartialEq)]
pub enum Command {
    Resolve { pkg_url: String, selectors: Vec<String> },
    Open { meta_far_blob_id: BlobId, selectors: Vec<String> },
    Repo(RepoCommand),
    Rule(RuleCommand),
}

#[derive(Debug, PartialEq)]
pub enum RepoCommand {
    Add { file: PathBuf },
    Remove { repo_url: String },
    List,
}

#[derive(Debug, PartialEq)]
pub enum RuleCommand {
    List,
    Clear,
    Replace { input_type: RuleConfigInputType },
}

#[derive(Debug, PartialEq)]
pub enum RuleConfigInputType {
    File { path: PathBuf },
    Json { config: RuleConfig },
}

impl From<RepoCommand> for Command {
    fn from(repo: RepoCommand) -> Command {
        Command::Repo(repo)
    }
}

impl From<RuleCommand> for Command {
    fn from(rule: RuleCommand) -> Command {
        Command::Rule(rule)
    }
}

impl From<RuleConfigInputType> for Command {
    fn from(rule: RuleConfigInputType) -> Command {
        Command::Rule(RuleCommand::Replace { input_type: rule })
    }
}

pub fn parse_args<'a, I>(mut iter: I) -> Result<Command, Error>
where
    I: Iterator<Item = &'a str>,
{
    macro_rules! unrecognized {
        ($arg:expr) => {
            return Err(Error::UnrecognizedArgument($arg.to_string()));
        };
    }

    macro_rules! done {
        ($e:expr) => {
            match iter.next() {
                None => $e,
                Some(arg) => unrecognized!(arg),
            }
        };
    }

    match iter.next().ok_or_else(|| Error::Help(HELP))? {
        "-h" | "--help" => done!(Err(Error::Help(HELP))),
        "help" => {
            let help = match iter.next() {
                None => HELP,
                Some("repo") => match iter.next() {
                    None => HELP_REPO,
                    Some("add") => done!(HELP_REPO_ADD),
                    Some("list") => done!(HELP_REPO_LIST),
                    Some("remove") => done!(HELP_REPO_REMOVE),
                    Some(arg) => unrecognized!(arg),
                },
                Some("resolve") => done!(HELP_RESOLVE),
                Some("rule") => match iter.next() {
                    None => HELP_RULE,
                    Some("clear") => done!(HELP_RULE_CLEAR),
                    Some("list") => done!(HELP_RULE_LIST),
                    Some("replace") => match iter.next() {
                        None => HELP_RULE_REPLACE,
                        Some("file") => done!(HELP_RULE_REPLACE_FILE),
                        Some("json") => done!(HELP_RULE_REPLACE_JSON),
                        Some(arg) => unrecognized!(arg),
                    },
                    Some(arg) => unrecognized!(arg),
                },
                Some("open") => done!(HELP_OPEN),
                Some(arg) => unrecognized!(arg),
            };

            Err(Error::Help(help))
        }
        "open" => {
            let meta_far_blob_id =
                iter.next().ok_or_else(|| Error::MissingArgument("meta_far_blob_id"))?;
            let selectors = iter.map(|s| s.to_string()).collect();

            Ok(Command::Open { meta_far_blob_id: meta_far_blob_id.parse()?, selectors })
        }
        "resolve" => {
            let pkg_url = iter.next().ok_or_else(|| Error::MissingArgument("pkg_url"))?;
            let selectors = iter.map(|s| s.to_string()).collect();
            Ok(Command::Resolve { pkg_url: pkg_url.to_string(), selectors })
        }
        "repo" => match iter.next() {
            None => Err(Error::MissingCommand),
            Some("list") => done!(Ok(RepoCommand::List.into())),
            Some("add") => match iter.next() {
                None => Err(Error::MissingArgument("file")),
                Some("-f") | Some("--file") => {
                    let file = iter.next().ok_or_else(|| Error::MissingArgument("file"))?;
                    done!(Ok(RepoCommand::Add { file: file.into() }.into()))
                }
                Some(arg) => unrecognized!(arg),
            },
            Some("remove") => match iter.next() {
                None => Err(Error::MissingArgument("repo-url")),
                Some("--repo-url") => {
                    let repo_url = iter.next().ok_or_else(|| Error::MissingArgument("repo-url"))?;
                    done!(Ok(RepoCommand::Remove { repo_url: repo_url.to_string() }.into()))
                }
                Some(arg) => unrecognized!(arg),
            },
            Some(arg) => unrecognized!(arg),
        },
        "rule" => match iter.next() {
            None => Err(Error::MissingCommand),
            Some("clear") => done!(Ok(RuleCommand::Clear.into())),
            Some("list") => done!(Ok(RuleCommand::List.into())),
            Some("replace") => match iter.next() {
                None => Err(Error::MissingCommand),
                Some("file") => {
                    let path = iter.next().ok_or_else(|| Error::MissingArgument("path"))?;
                    done!(Ok(RuleConfigInputType::File { path: path.into() }.into()))
                }
                Some("json") => {
                    let config = iter.next().ok_or_else(|| Error::MissingArgument("config"))?;
                    let config = serde_json::from_str(&config)?;
                    done!(Ok(RuleConfigInputType::Json { config }.into()))
                }
                Some(arg) => unrecognized!(arg),
            },
            Some(arg) => unrecognized!(arg),
        },
        arg => unrecognized!(arg),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const REPO_URL: &str = "fuchsia-pkg://fuchsia.com";
    const CONFIG_JSON: &str = r#"{"version": "1", "content": []}"#;

    #[test]
    fn test_unrecognized_argument() {
        fn check(args: &[&str]) {
            for expected in &["foo", "--foo"] {
                let args = args.iter().chain(Some(expected)).collect::<Vec<_>>();

                match parse_args(args.iter().map(|s| &***s)) {
                    Err(Error::UnrecognizedArgument(arg)) => {
                        assert_eq!(&arg, expected, "arg: {:?}", args);
                    }
                    result => panic!("unexpected result {:?} for {:?}", result, args),
                }
            }
        }

        // note, specifically leaving out "open" and "resolve" since they accept arbitrary
        // selectors.
        let cases: &[&[&str]] = &[
            &[],
            &["help"],
            &["help", "repo"],
            &["help", "repo", "add"],
            &["help", "repo", "list"],
            &["help", "repo", "remove"],
            &["help", "rule"],
            &["help", "rule", "clear"],
            &["help", "rule", "list"],
            &["help", "rule", "replace"],
            &["help", "rule", "replace", "file"],
            &["help", "rule", "replace", "json"],
            &["help", "open"],
            &["repo", "add"],
            &["repo", "add", "-f", "foo"],
            &["repo", "add", "--file", "foo"],
            &["repo", "list"],
            &["repo", "remove"],
            &["repo", "remove", "--repo-url", REPO_URL],
            &["repo"],
            &["rule", "clear"],
            &["rule", "list"],
            &["rule", "replace"],
            &["rule", "replace", "file", "foo"],
            &["rule", "replace", "json", CONFIG_JSON],
            &["rule"],
        ];

        for case in cases {
            check(case);
        }
    }

    #[test]
    fn test_help() {
        fn check(args: &[&str], expected: &str) {
            match parse_args(args.into_iter().map(|s| *s)) {
                Err(Error::Help(msg)) => {
                    assert_eq!(msg, expected);
                }
                result => panic!("unexpected result {:?}", result),
            };
        }

        check(&[], HELP);
        check(&["-h"], HELP);
        check(&["--help"], HELP);
        check(&["help"], HELP);

        check(&["help", "open"], HELP_OPEN);
        check(&["help", "repo"], HELP_REPO);
        check(&["help", "repo", "add"], HELP_REPO_ADD);
        check(&["help", "repo", "list"], HELP_REPO_LIST);
        check(&["help", "repo", "remove"], HELP_REPO_REMOVE);
        check(&["help", "resolve"], HELP_RESOLVE);
        check(&["help", "rule"], HELP_RULE);
        check(&["help", "rule", "clear"], HELP_RULE_CLEAR);
        check(&["help", "rule", "replace"], HELP_RULE_REPLACE);
        check(&["help", "rule", "replace", "file"], HELP_RULE_REPLACE_FILE);
        check(&["help", "rule", "replace", "json"], HELP_RULE_REPLACE_JSON);
    }

    #[test]
    fn test_resolve() {
        fn check(args: &[&str], expected_pkg_url: &str, expected_selectors: &[String]) {
            match parse_args(args.into_iter().map(|s| *s)) {
                Ok(Command::Resolve { pkg_url, selectors }) => {
                    assert_eq!(&pkg_url, expected_pkg_url);
                    assert_eq!(selectors, expected_selectors);
                }
                result => panic!("unexpected result {:?}", result),
            };
        }

        let url = "fuchsia-pkg://fuchsia.com/foo/bar";

        check(&["resolve", url], url, &[]);

        check(
            &["resolve", url, "selector1", "selector2"],
            url,
            &["selector1".to_string(), "selector2".to_string()],
        );
    }

    #[test]
    fn test_open() {
        fn check(args: &[&str], expected_blob_id: &str, expected_selectors: &[String]) {
            match parse_args(args.into_iter().map(|s| *s)) {
                Ok(Command::Open { meta_far_blob_id, selectors }) => {
                    let expected_blob_id: BlobId = expected_blob_id.parse().unwrap();
                    assert_eq!(meta_far_blob_id, expected_blob_id);
                    assert_eq!(selectors, expected_selectors);
                }
                result => panic!("unexpected result {:?}", result),
            };
        }

        let blob_id = "1111111111111111111111111111111111111111111111111111111111111111";
        check(&["open", blob_id], blob_id, &[]);

        check(
            &["open", blob_id, "selector1", "selector2"],
            blob_id,
            &["selector1".to_string(), "selector2".to_string()],
        );
    }

    #[test]
    fn test_open_reject_malformed_blobs() {
        match parse_args(["open", "bad_id"].iter().map(|a| *a)) {
            Err(Error::BlobId(_)) => {}
            result => panic!("unexpected result {:?}", result),
        }
    }

    #[test]
    fn test_repo() {
        fn check(args: &[&str], expected: RepoCommand) {
            match parse_args(args.into_iter().map(|s| *s)) {
                Ok(Command::Repo(cmd)) => {
                    assert_eq!(cmd, expected);
                }
                result => panic!("unexpected result {:?}", result),
            };
        }

        check(&["repo", "list"], RepoCommand::List);
        check(&["repo", "add", "-f", "foo"], RepoCommand::Add { file: "foo".into() });
        check(&["repo", "add", "--file", "foo"], RepoCommand::Add { file: "foo".into() });

        check(
            &["repo", "remove", "--repo-url", REPO_URL],
            RepoCommand::Remove { repo_url: REPO_URL.to_string() },
        );
    }

    #[test]
    fn test_rule() {
        fn check(args: &[&str], expected: RuleCommand) {
            match parse_args(args.into_iter().map(|s| *s)) {
                Ok(Command::Rule(cmd)) => {
                    assert_eq!(cmd, expected);
                }
                result => panic!("unexpected result {:?}", result),
            };
        }

        check(&["rule", "list"], RuleCommand::List);
        check(&["rule", "clear"], RuleCommand::Clear);
        check(
            &["rule", "replace", "file", "foo"],
            RuleCommand::Replace { input_type: RuleConfigInputType::File { path: "foo".into() } },
        );
        check(
            &["rule", "replace", "json", CONFIG_JSON],
            RuleCommand::Replace {
                input_type: RuleConfigInputType::Json { config: RuleConfig::Version1(vec![]) },
            },
        );
    }

    #[test]
    fn test_rule_replace_json_rejects_malformed_json() {
        match parse_args(["rule", "replace", "json", "{"].iter().map(|a| *a)) {
            Err(Error::Json(_)) => {}
            result => panic!("unexpected result {:?}", result),
        }
    }
}
