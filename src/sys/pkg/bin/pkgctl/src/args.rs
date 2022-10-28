// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::Error, argh::FromArgs, fidl_fuchsia_pkg::ExperimentToggle as Experiment,
    fidl_fuchsia_pkg_ext::BlobId, fidl_fuchsia_pkg_rewrite_ext::RuleConfig, std::path::PathBuf,
};

#[derive(FromArgs, Debug, PartialEq)]
/// Various operations on packages, package repositories, and the package cache.
pub struct Args {
    #[argh(subcommand)]
    pub command: Command,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum Command {
    Resolve(ResolveCommand),
    Open(OpenCommand),
    Repo(RepoCommand),
    Rule(RuleCommand),
    Experiment(ExperimentCommand),
    Gc(GcCommand),
    GetHash(GetHashCommand),
    PkgStatus(PkgStatusCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "resolve")]
/// Resolve a package.
pub struct ResolveCommand {
    #[argh(positional)]
    pub pkg_url: String,

    /// prints the contents of the resolved package, which can be slow for large packages.
    #[argh(switch, short = 'v')]
    pub verbose: bool,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "open")]
/// Open a package by merkle root.
pub struct OpenCommand {
    #[argh(positional)]
    pub meta_far_blob_id: BlobId,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "repo",
    note = "A fuchsia package URL contains a repository hostname to identify the package's source.\n",
    note = "Without any arguments the command outputs the list of configured repository URLs.\n",
    note = "Note that repo commands expect the full repository URL, not just the hostname, e.g:",
    note = "$ pkgctl repo rm fuchsia-pkg://example.com"
)]
/// Manage one or more known repositories.
pub struct RepoCommand {
    /// verbose output
    #[argh(switch, short = 'v')]
    pub verbose: bool,

    #[argh(subcommand)]
    pub subcommand: Option<RepoSubCommand>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum RepoSubCommand {
    Add(RepoAddCommand),
    Remove(RepoRemoveCommand),
    Show(RepoShowCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "add")]
/// Add a source repository.
pub struct RepoAddCommand {
    #[argh(subcommand)]
    pub subcommand: RepoAddSubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum RepoAddSubCommand {
    File(RepoAddFileCommand),
    Url(RepoAddUrlCommand),
}

#[derive(Debug, PartialEq)]
pub enum RepoConfigFormat {
    Version1,
    Version2,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "file")]
/// Add a repository config from a local file, in JSON format, which contains the different repository metadata and URLs.
pub struct RepoAddFileCommand {
    /// persist TUF metadata for repositories provided to the RepoManager.
    #[argh(switch, short = 'p')]
    pub persist: bool,
    /// the expected config.json file format version.
    #[argh(
        option,
        short = 'f',
        default = "RepoConfigFormat::Version2",
        from_str_fn(repo_config_format)
    )]
    pub format: RepoConfigFormat,
    /// name of the source (a name from the URL will be derived if not provided).
    #[argh(option, short = 'n')]
    pub name: Option<String>,
    /// repository config file, in JSON format, which contains the different repository metadata and URLs.
    #[argh(positional)]
    pub file: PathBuf,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "url")]
/// Add a repository config via http, in JSON format, which contains the different repository metadata and URLs.
pub struct RepoAddUrlCommand {
    /// persist TUF metadata for repositories provided to the RepoManager.
    #[argh(switch, short = 'p')]
    pub persist: bool,
    /// the expected config.json file format version.
    #[argh(
        option,
        short = 'f',
        default = "RepoConfigFormat::Version2",
        from_str_fn(repo_config_format)
    )]
    pub format: RepoConfigFormat,
    /// name of the source (a name from the URL will be derived if not provided).
    #[argh(option, short = 'n')]
    pub name: Option<String>,
    /// http(s) URL pointing to a repository config file, in JSON format, which contains the different repository metadata and URLs.
    #[argh(positional)]
    pub repo_url: String,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "rm")]
/// Remove a configured source repository.
pub struct RepoRemoveCommand {
    #[argh(positional)]
    pub repo_url: String,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "show")]
/// Show JSON-formatted details of a configured source repository.
pub struct RepoShowCommand {
    #[argh(positional)]
    pub repo_url: String,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "rule")]
/// Manage URL rewrite rules applied to package URLs during package resolution.
pub struct RuleCommand {
    #[argh(subcommand)]
    pub subcommand: RuleSubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum RuleSubCommand {
    Clear(RuleClearCommand),
    DumpDynamic(RuleDumpDynamicCommand),
    List(RuleListCommand),
    Replace(RuleReplaceCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "clear")]
/// Clear all URL rewrite rules.
pub struct RuleClearCommand {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "list")]
/// List all URL rewrite rules.
pub struct RuleListCommand {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "dump-dynamic")]
/// Dumps all dynamic rewrite rules.
pub struct RuleDumpDynamicCommand {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "replace")]
/// Replace all dynamic rules with the provided rules.
pub struct RuleReplaceCommand {
    #[argh(subcommand)]
    pub subcommand: RuleReplaceSubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum RuleReplaceSubCommand {
    File(RuleReplaceFileCommand),
    Json(RuleReplaceJsonCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "file")]
/// Replace all rewrite rules with ones specified in a file
pub struct RuleReplaceFileCommand {
    #[argh(positional)]
    pub file: PathBuf,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "json")]
/// Replace all rewrite rules with JSON from the command line
pub struct RuleReplaceJsonCommand {
    #[argh(positional, from_str_fn(parse_rule_config))]
    pub config: RuleConfig,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "experiment",
    note = "Experiments may be added or removed over time and should not be considered stable.",
    note = "Known experiments:",
    note = "  lightbulb      no-op experiment"
)]
/// Manage runtime experiment states.
pub struct ExperimentCommand {
    #[argh(subcommand)]
    pub subcommand: ExperimentSubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum ExperimentSubCommand {
    Enable(ExperimentEnableCommand),
    Disable(ExperimentDisableCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "enable")]
/// Enable the given experiment.
pub struct ExperimentEnableCommand {
    #[argh(positional, from_str_fn(parse_experiment_id))]
    pub experiment: Experiment,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "disable")]
/// Disable the given experiment.
pub struct ExperimentDisableCommand {
    #[argh(positional, from_str_fn(parse_experiment_id))]
    pub experiment: Experiment,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "gc",
    note = "This deletes any cached packages that are not present in the static and dynamic index.",
    note = "Any blobs associated with these packages will be removed if they are not referenced by another component or package.",
    note = "The static index currently is located at /system/data/static_packages, but this location is likely to change.",
    note = "The dynamic index is dynamically calculated, and cannot easily be queried at this time."
)]
/// Trigger a manual garbage collection of the package cache.
pub struct GcCommand {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "get-hash")]
/// Get the hash of a package.
pub struct GetHashCommand {
    #[argh(positional)]
    pub pkg_url: String,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "pkg-status",
    note = "Exit codes:",
    note = "    0 - pkg in tuf repo and on disk",
    note = "    2 - pkg in tuf repo but not on disk",
    note = "    3 - pkg not in tuf repo",
    note = "    1 - any other misc application error"
)]
/// Determine if a pkg is in a registered tuf repo and/or on disk.
pub struct PkgStatusCommand {
    #[argh(positional)]
    pub pkg_url: String,
}

fn parse_experiment_id(experiment: &str) -> Result<Experiment, String> {
    match experiment {
        "lightbulb" => Ok(Experiment::Lightbulb),
        experiment => Err(Error::ExperimentId(experiment.to_owned()).to_string()),
    }
}

fn parse_rule_config(config: &str) -> Result<RuleConfig, String> {
    serde_json::from_str(config).map_err(|e| e.to_string())
}

fn repo_config_format(value: &str) -> Result<RepoConfigFormat, String> {
    match value {
        "1" => Ok(RepoConfigFormat::Version1),
        "2" => Ok(RepoConfigFormat::Version2),
        _ => Err(format!("unknown format {:?}", value)),
    }
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches};

    const REPO_URL: &str = "fuchsia-pkg://fuchsia.com";
    const CONFIG_JSON: &str = r#"{"version": "1", "content": []}"#;
    const CMD_NAME: &[&str] = &["pkgctl"];

    #[test]
    fn resolve() {
        fn check(args: &[&str], expected_pkg_url: &str, expected_verbose: bool) {
            assert_eq!(
                Args::from_args(CMD_NAME, args),
                Ok(Args {
                    command: Command::Resolve(ResolveCommand {
                        pkg_url: expected_pkg_url.to_string(),
                        verbose: expected_verbose,
                    })
                })
            );
        }

        let url = "fuchsia-pkg://fuchsia.com/foo/bar";

        check(&["resolve", url], url, false);
        check(&["resolve", "--verbose", url], url, true);
        check(&["resolve", "-v", url], url, true);
    }

    #[test]
    fn open() {
        fn check(args: &[&str], expected_blob_id: &str) {
            assert_eq!(
                Args::from_args(CMD_NAME, args),
                Ok(Args {
                    command: Command::Open(OpenCommand {
                        meta_far_blob_id: expected_blob_id.parse().unwrap(),
                    })
                })
            )
        }

        let blob_id = "1111111111111111111111111111111111111111111111111111111111111111";
        check(&["open", blob_id], blob_id);

        check(&["open", blob_id], blob_id);
    }

    #[test]
    fn open_reject_malformed_blobs() {
        match Args::from_args(CMD_NAME, &["open", "bad_id"]) {
            Err(argh::EarlyExit { output: _, status: _ }) => {}
            result => panic!("unexpected result {:?}", result),
        }
    }

    #[test]
    fn repo() {
        fn check(args: &[&str], expected: RepoCommand) {
            assert_eq!(
                Args::from_args(CMD_NAME, args),
                Ok(Args { command: Command::Repo(expected) })
            )
        }

        check(&["repo"], RepoCommand { verbose: false, subcommand: None });
        check(&["repo", "-v"], RepoCommand { verbose: true, subcommand: None });
        check(&["repo", "--verbose"], RepoCommand { verbose: true, subcommand: None });
        check(
            &["repo", "add", "file", "foo"],
            RepoCommand {
                verbose: false,
                subcommand: Some(RepoSubCommand::Add(RepoAddCommand {
                    subcommand: RepoAddSubCommand::File(RepoAddFileCommand {
                        persist: false,
                        format: RepoConfigFormat::Version2,
                        name: None,
                        file: "foo".into(),
                    }),
                })),
            },
        );
        check(
            &["repo", "add", "file", "-f", "1", "foo"],
            RepoCommand {
                verbose: false,
                subcommand: Some(RepoSubCommand::Add(RepoAddCommand {
                    subcommand: RepoAddSubCommand::File(RepoAddFileCommand {
                        persist: false,
                        format: RepoConfigFormat::Version1,
                        name: None,
                        file: "foo".into(),
                    }),
                })),
            },
        );
        check(
            &["repo", "add", "file", "-p", "foo"],
            RepoCommand {
                verbose: false,
                subcommand: Some(RepoSubCommand::Add(RepoAddCommand {
                    subcommand: RepoAddSubCommand::File(RepoAddFileCommand {
                        persist: true,
                        format: RepoConfigFormat::Version2,
                        name: None,
                        file: "foo".into(),
                    }),
                })),
            },
        );
        check(
            &["repo", "add", "file", "-p", "-f", "1", "foo"],
            RepoCommand {
                verbose: false,
                subcommand: Some(RepoSubCommand::Add(RepoAddCommand {
                    subcommand: RepoAddSubCommand::File(RepoAddFileCommand {
                        persist: true,
                        format: RepoConfigFormat::Version1,
                        name: None,
                        file: "foo".into(),
                    }),
                })),
            },
        );
        check(
            &["repo", "add", "file", "-n", "devhost", "foo"],
            RepoCommand {
                verbose: false,
                subcommand: Some(RepoSubCommand::Add(RepoAddCommand {
                    subcommand: RepoAddSubCommand::File(RepoAddFileCommand {
                        persist: false,
                        format: RepoConfigFormat::Version2,
                        name: Some("devhost".to_string()),
                        file: "foo".into(),
                    }),
                })),
            },
        );
        check(
            &["repo", "add", "url", "-n", "devhost", "http://foo.tld/fuchsia/config.json"],
            RepoCommand {
                verbose: false,
                subcommand: Some(RepoSubCommand::Add(RepoAddCommand {
                    subcommand: RepoAddSubCommand::Url(RepoAddUrlCommand {
                        persist: false,
                        format: RepoConfigFormat::Version2,
                        name: Some("devhost".to_string()),
                        repo_url: "http://foo.tld/fuchsia/config.json".into(),
                    }),
                })),
            },
        );
        check(
            &["repo", "add", "url", "-p", "-n", "devhost", "http://foo.tld/fuchsia/config.json"],
            RepoCommand {
                verbose: false,
                subcommand: Some(RepoSubCommand::Add(RepoAddCommand {
                    subcommand: RepoAddSubCommand::Url(RepoAddUrlCommand {
                        persist: true,
                        format: RepoConfigFormat::Version2,
                        name: Some("devhost".to_string()),
                        repo_url: "http://foo.tld/fuchsia/config.json".into(),
                    }),
                })),
            },
        );
        check(
            &[
                "repo",
                "add",
                "url",
                "-p",
                "-f",
                "1",
                "-n",
                "devhost",
                "http://foo.tld/fuchsia/config.json",
            ],
            RepoCommand {
                verbose: false,
                subcommand: Some(RepoSubCommand::Add(RepoAddCommand {
                    subcommand: RepoAddSubCommand::Url(RepoAddUrlCommand {
                        persist: true,
                        format: RepoConfigFormat::Version1,
                        name: Some("devhost".to_string()),
                        repo_url: "http://foo.tld/fuchsia/config.json".into(),
                    }),
                })),
            },
        );
        check(
            &["repo", "rm", REPO_URL],
            RepoCommand {
                verbose: false,
                subcommand: Some(RepoSubCommand::Remove(RepoRemoveCommand {
                    repo_url: REPO_URL.to_string(),
                })),
            },
        );
        check(
            &["repo", "show", REPO_URL],
            RepoCommand {
                verbose: false,
                subcommand: Some(RepoSubCommand::Show(RepoShowCommand {
                    repo_url: REPO_URL.to_string(),
                })),
            },
        );
    }

    #[test]
    fn rule() {
        fn check(args: &[&str], expected: RuleCommand) {
            match Args::from_args(CMD_NAME, args).unwrap() {
                Args { command: Command::Rule(cmd) } => {
                    assert_eq!(cmd, expected);
                }
                result => panic!("unexpected result {:?}", result),
            }
        }

        check(
            &["rule", "list"],
            RuleCommand { subcommand: RuleSubCommand::List(RuleListCommand {}) },
        );
        check(
            &["rule", "clear"],
            RuleCommand { subcommand: RuleSubCommand::Clear(RuleClearCommand {}) },
        );
        check(
            &["rule", "dump-dynamic"],
            RuleCommand { subcommand: RuleSubCommand::DumpDynamic(RuleDumpDynamicCommand {}) },
        );
        check(
            &["rule", "replace", "file", "foo"],
            RuleCommand {
                subcommand: RuleSubCommand::Replace(RuleReplaceCommand {
                    subcommand: RuleReplaceSubCommand::File(RuleReplaceFileCommand {
                        file: "foo".into(),
                    }),
                }),
            },
        );
        check(
            &["rule", "replace", "json", CONFIG_JSON],
            RuleCommand {
                subcommand: RuleSubCommand::Replace(RuleReplaceCommand {
                    subcommand: RuleReplaceSubCommand::Json(RuleReplaceJsonCommand {
                        config: RuleConfig::Version1(vec![]),
                    }),
                }),
            },
        );
    }

    #[test]
    fn rule_replace_json_rejects_malformed_json() {
        assert_matches!(
            Args::from_args(CMD_NAME, &["rule", "replace", "json", "{"]),
            Err(argh::EarlyExit { output: _, status: _ })
        );
    }

    #[test]
    fn experiment_ok() {
        assert_eq!(
            Args::from_args(CMD_NAME, &["experiment", "enable", "lightbulb"]).unwrap(),
            Args {
                command: Command::Experiment(ExperimentCommand {
                    subcommand: ExperimentSubCommand::Enable(ExperimentEnableCommand {
                        experiment: Experiment::Lightbulb
                    })
                })
            }
        );

        assert_eq!(
            Args::from_args(CMD_NAME, &["experiment", "disable", "lightbulb"]).unwrap(),
            Args {
                command: Command::Experiment(ExperimentCommand {
                    subcommand: ExperimentSubCommand::Disable(ExperimentDisableCommand {
                        experiment: Experiment::Lightbulb
                    })
                })
            }
        );
    }

    #[test]
    fn experiment_unknown() {
        assert_matches!(
            Args::from_args(CMD_NAME, &["experiment", "enable", "unknown"]),
            Err(argh::EarlyExit { output, status: Err(()) }) if output.contains("unknown")
        );

        assert_matches!(
            Args::from_args(CMD_NAME, &["experiment", "disable", "unknown"]),
            Err(argh::EarlyExit { output, status: Err(()) }) if output.contains("unknown")
        );
    }

    #[test]
    fn gc() {
        match Args::from_args(CMD_NAME, &["gc"]).unwrap() {
            Args { command: Command::Gc(GcCommand {}) } => {}
            result => panic!("unexpected result {:?}", result),
        }
    }

    #[test]
    fn get_hash() {
        let url = "fuchsia-pkg://fuchsia.com/foo/bar";
        match Args::from_args(CMD_NAME, &["get-hash", url]).unwrap() {
            Args { command: Command::GetHash(GetHashCommand { pkg_url }) } if pkg_url == url => {}
            result => panic!("unexpected result {:?}", result),
        }
    }

    #[test]
    fn pkg_status() {
        let url = "fuchsia-pkg://fuchsia.com/foo/bar";
        match Args::from_args(CMD_NAME, &["pkg-status", url]).unwrap() {
            Args { command: Command::PkgStatus(PkgStatusCommand { pkg_url }) }
                if pkg_url == url => {}
            result => panic!("unexpected result {:?}", result),
        }
    }
}
