// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::Error, argh, argh::FromArgs, fidl_fuchsia_pkg::ExperimentToggle as Experiment,
    fidl_fuchsia_pkg_ext::BlobId, fidl_fuchsia_pkg_rewrite_ext::RuleConfig, serde_json,
    std::path::PathBuf,
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
    Update(UpdateCommand),
    Gc(GcCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "resolve")]
/// Resolve a package.
pub struct ResolveCommand {
    #[argh(positional)]
    pub pkg_url: String,

    #[argh(positional)]
    pub selectors: Vec<String>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "open")]
/// Open a package by merkle root.
pub struct OpenCommand {
    #[argh(positional)]
    pub meta_far_blob_id: BlobId,

    #[argh(positional)]
    pub selectors: Vec<String>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "repo",
    note = "A fuchsia package URL contains a repository hostname to identify the package's source.\n",
    note = "Example repository hostnames are:\n",
    note = "    fuchsia.com",
    note = "    mycorp.com\n",
    note = "Without any arguments the command outputs the list of configured repository URLs."
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
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "add")]
/// Add a source repository.
pub struct RepoAddCommand {
    /// path to a respository config file, in JSON format, which contains the different repository metadata and URLs.
    #[argh(option, short = 'f')]
    pub file: PathBuf,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "rm")]
/// Remove a configured source repository.
pub struct RepoRemoveCommand {
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
    note = "  lightbulb      no-op experiment",
    note = "  rust-tuf       use rust-tuf to resolve package merkle roots"
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
    name = "update",
    note = "This command is non-blocking. View the syslog for more detailed progress information."
)]
/// Perform a system update check and trigger an OTA if available.
pub struct UpdateCommand {}

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

fn parse_experiment_id(experiment: &str) -> Result<Experiment, String> {
    match experiment {
        "lightbulb" => Ok(Experiment::Lightbulb),
        "rust-tuf" => Ok(Experiment::RustTuf),
        experiment => Err(Error::ExperimentId(experiment.to_owned()).to_string()),
    }
}

fn parse_rule_config(config: &str) -> Result<RuleConfig, String> {
    serde_json::from_str(&config).map_err(|e| e.to_string())
}

#[cfg(test)]
mod tests {
    use {super::*, matches::assert_matches};

    const REPO_URL: &str = "fuchsia-pkg://fuchsia.com";
    const CONFIG_JSON: &str = r#"{"version": "1", "content": []}"#;
    const CMD_NAME: &'static [&'static str] = &["pkgctl"];

    #[test]
    fn test_resolve() {
        fn check(args: &[&str], expected_pkg_url: &str, expected_selectors: &[String]) {
            assert_eq!(
                Args::from_args(CMD_NAME, args),
                Ok(Args {
                    command: Command::Resolve(ResolveCommand {
                        pkg_url: expected_pkg_url.to_string(),
                        selectors: expected_selectors.into_iter().cloned().collect()
                    })
                })
            );
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
            assert_eq!(
                Args::from_args(CMD_NAME, args),
                Ok(Args {
                    command: Command::Open(OpenCommand {
                        meta_far_blob_id: expected_blob_id.parse().unwrap(),
                        selectors: expected_selectors.into_iter().cloned().collect()
                    })
                })
            )
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
        match Args::from_args(CMD_NAME, &["open", "bad_id"]) {
            Err(argh::EarlyExit { output: _, status: _ }) => {}
            result => panic!("unexpected result {:?}", result),
        }
    }

    #[test]
    fn test_repo() {
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
            &["repo", "add", "-f", "foo"],
            RepoCommand {
                verbose: false,
                subcommand: Some(RepoSubCommand::Add(RepoAddCommand { file: "foo".into() })),
            },
        );
        check(
            &["repo", "add", "--file", "foo"],
            RepoCommand {
                verbose: false,
                subcommand: Some(RepoSubCommand::Add(RepoAddCommand { file: "foo".into() })),
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
    }

    #[test]
    fn test_rule() {
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
    fn test_rule_replace_json_rejects_malformed_json() {
        assert_matches!(
            Args::from_args(CMD_NAME, &["rule", "replace", "json", "{"]),
            Err(argh::EarlyExit { output: _, status: _ })
        );
    }

    #[test]
    fn test_experiment_ok() {
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
    fn test_experiment_unknown() {
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
    fn test_update() {
        match Args::from_args(CMD_NAME, &["update"]).unwrap() {
            Args { command: Command::Update(UpdateCommand {}) } => {}
            result => panic!("unexpected result {:?}", result),
        }
    }

    #[test]
    fn test_gc() {
        match Args::from_args(CMD_NAME, &["gc"]).unwrap() {
            Args { command: Command::Gc(GcCommand {}) } => {}
            result => panic!("unexpected result {:?}", result),
        }
    }
}
