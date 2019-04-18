// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![deny(warnings)]

use failure::{Error, Fail, ResultExt};
use fidl_fuchsia_pkg::{
    PackageCacheMarker, PackageResolverMarker, RepositoryManagerMarker, UpdatePolicy,
};
use fidl_fuchsia_pkg_ext::{BlobId, RepositoryConfig};
use fidl_fuchsia_pkg_rewrite::{EditTransactionProxy, EngineMarker, EngineProxy};
use files_async;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_uri_rewrite::{Rule as RewriteRule, RuleConfig};
use fuchsia_zircon as zx;
use futures::Future;
use serde_json;
use std::convert::{TryFrom, TryInto};
use std::fs::File;
use std::path::PathBuf;
use structopt::StructOpt;

#[derive(StructOpt)]
#[structopt(name = "pkgctl")]
struct Options {
    #[structopt(subcommand)]
    cmd: Command,
}

#[derive(StructOpt)]
enum Command {
    #[structopt(name = "resolve", about = "resolve a package")]
    Resolve {
        #[structopt(help = "URI of package to cache")]
        pkg_uri: String,

        #[structopt(help = "Package selectors")]
        selectors: Vec<String>,
    },

    #[structopt(name = "open", about = "open a package by merkle root")]
    Open {
        #[structopt(help = "Merkle root of package's meta.far to cache")]
        meta_far_blob_id: BlobId,

        #[structopt(help = "Package selectors")]
        selectors: Vec<String>,
    },

    #[structopt(name = "repo", about = "repo subcommands")]
    Repo(RepoCommand),

    #[structopt(name = "rule", about = "manage URI rewrite rules")]
    Rule(RuleCommand),
}

#[derive(StructOpt)]
enum RepoCommand {
    #[structopt(name = "add", about = "add a repository")]
    Add {
        #[structopt(short = "f", long = "file", help = "path to a repository config file")]
        file: PathBuf,
    },

    #[structopt(name = "add", about = "add a repository")]
    Remove {
        #[structopt(long = "repo-url", help = "the repository url to remove")]
        repo_url: String,
    },

    #[structopt(name = "list", about = "list repositories")]
    List,
}

#[derive(StructOpt)]
enum RuleCommand {
    #[structopt(name = "list", about = "list all rules")]
    List,

    #[structopt(name = "clear", about = "clear all rules")]
    Clear,

    #[structopt(name = "replace", about = "replace all dynamic rules with the provided rules")]
    Replace {
        #[structopt(subcommand)]
        input_type: RuleConfigInputType,
    },
}

#[derive(StructOpt)]
enum RuleConfigInputType {
    #[structopt(name = "file")]
    File {
        #[structopt(help = "path to rewrite rule config file")]
        path: PathBuf,
    },

    #[structopt(name = "json")]
    Json {
        #[structopt(
            help = "JSON encoded rewrite rule config",
            parse(try_from_str = "parse_rule_config")
        )]
        config: RuleConfig,
    },
}

fn parse_rule_config(s: &str) -> Result<RuleConfig, serde_json::error::Error> {
    serde_json::from_str(s)
}

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("Error creating executor")?;

    let Options { cmd } = Options::from_args();

    let fut = async {
        match cmd {
            Command::Resolve { pkg_uri, selectors } => {
                let resolver = connect_to_service::<PackageResolverMarker>()
                    .context("Failed to connect to resolver service")?;
                println!("resolving {} with the selectors {:?}", pkg_uri, selectors);

                let (dir, dir_server_end) = fidl::endpoints::create_proxy()?;

                let res = await!(resolver.resolve(
                    &pkg_uri,
                    &mut selectors.iter().map(|s| s.as_str()),
                    &mut UpdatePolicy { fetch_if_absent: true, allow_old_versions: true },
                    dir_server_end,
                ))?;
                zx::Status::ok(res)?;

                let entries = await!(files_async::readdir_recursive(dir))?;
                println!("package contents:");
                for entry in entries {
                    println!("/{:?}", entry);
                }

                Ok(())
            }
            Command::Open { meta_far_blob_id, selectors } => {
                let cache = connect_to_service::<PackageCacheMarker>()
                    .context("Failed to connect to cache service")?;
                println!("opening {} with the selectors {:?}", meta_far_blob_id, selectors);

                let (dir, dir_server_end) = fidl::endpoints::create_proxy()?;

                let res = await!(cache.open(
                    &mut meta_far_blob_id.into(),
                    &mut selectors.iter().map(|s| s.as_str()),
                    dir_server_end,
                ))?;
                zx::Status::ok(res)?;

                let entries = await!(files_async::readdir_recursive(dir))?;
                println!("package contents:");
                for entry in entries {
                    println!("/{:?}", entry);
                }

                Ok(())
            }
            Command::Repo(cmd) => {
                let repo_manager = connect_to_service::<RepositoryManagerMarker>()
                    .context("Failed to connect to resolver service")?;

                match cmd {
                    RepoCommand::Add { file } => {
                        let repo: RepositoryConfig = serde_json::from_reader(File::open(file)?)?;

                        let res = await!(repo_manager.add(repo.into()))?;
                        zx::Status::ok(res)?;

                        Ok(())
                    }

                    RepoCommand::Remove { repo_url } => {
                        let res = await!(repo_manager.remove(&repo_url))?;
                        zx::Status::ok(res)?;

                        Ok(())
                    }

                    RepoCommand::List => {
                        let (iter, server_end) = fidl::endpoints::create_proxy()?;
                        repo_manager.list(server_end)?;
                        let mut repos = vec![];

                        loop {
                            let chunk = await!(iter.next())?;
                            if chunk.is_empty() {
                                break;
                            }
                            repos.extend(chunk);
                        }

                        let repos = repos
                            .into_iter()
                            .map(|repo| {
                                RepositoryConfig::try_from(repo).expect("valid repo config")
                            })
                            .collect::<Vec<_>>();

                        let s = serde_json::to_string_pretty(&repos).expect("valid json");
                        println!("{}", s);

                        Ok(())
                    }
                }
            }
            Command::Rule(cmd) => {
                let engine = connect_to_service::<EngineMarker>()
                    .context("Failed to connect to rewrite engine service")?;

                match cmd {
                    RuleCommand::List => {
                        let (iter, iter_server_end) = fidl::endpoints::create_proxy()?;
                        engine.list(iter_server_end)?;

                        let mut rules = Vec::new();
                        loop {
                            let more = await!(iter.next())?;
                            if more.is_empty() {
                                break;
                            }
                            rules.extend(more);
                        }
                        let rules = rules
                            .into_iter()
                            .map(|rule| rule.try_into())
                            .collect::<Result<Vec<RewriteRule>, _>>()?;

                        for rule in rules {
                            println!("{:#?}", rule);
                        }
                    }
                    RuleCommand::Clear => {
                        await!(do_transaction(engine, async move |transaction| {
                            transaction.reset_all()?;
                            Ok(transaction)
                        }))?;
                    }
                    RuleCommand::Replace { input_type } => {
                        let RuleConfig::Version1(ref rules) = match input_type {
                            RuleConfigInputType::File { path } => {
                                serde_json::from_reader(File::open(path)?)?
                            }
                            RuleConfigInputType::Json { config } => config,
                        };

                        await!(do_transaction(engine, async move |transaction| {
                            transaction.reset_all()?;
                            // add() inserts rules as highest priority, so iterate over our
                            // prioritized list of rules so they end up in the right order.
                            for rule in rules.iter().rev() {
                                await!(transaction.add(&mut rule.clone().into()))?;
                            }
                            Ok(transaction)
                        }))?;
                    }
                }

                Ok(())
            }
        }
    };

    executor.run_singlethreaded(fut)
}

#[derive(Debug, Fail)]
enum EditTransactionError {
    #[fail(display = "internal fidl error: {}", _0)]
    Fidl(#[cause] fidl::Error),

    #[fail(display = "commit error: {}", _0)]
    CommitError(zx::Status),
}

impl From<fidl::Error> for EditTransactionError {
    fn from(x: fidl::Error) -> Self {
        EditTransactionError::Fidl(x)
    }
}

/// Perform a rewrite rule edit transaction, retrying as necessary if another edit transaction runs
/// concurrently.
///
/// The given callback `cb` should perform the needed edits to the state of the rewrite rules but
/// not attempt to `commit()` the transaction. `do_transaction` will internally attempt to commit
/// the transaction and trigger a retry if necessary.
async fn do_transaction<T, R>(engine: EngineProxy, cb: T) -> Result<(), EditTransactionError>
where
    T: Fn(EditTransactionProxy) -> R,
    R: Future<Output = Result<EditTransactionProxy, fidl::Error>>,
{
    // Make a reasonable effort to retry the edit after a concurrent edit, but don't retry forever.
    for _ in 0..100 {
        let (transaction, transaction_server_end) = fidl::endpoints::create_proxy()?;
        engine.start_edit_transaction(transaction_server_end)?;

        let transaction = await!(cb(transaction))?;

        let status = await!(transaction.commit())?;

        // Retry edit transaction on concurrent edit
        return match zx::Status::from_raw(status) {
            zx::Status::OK => Ok(()),
            zx::Status::UNAVAILABLE => {
                continue;
            }
            status => Err(EditTransactionError::CommitError(status)),
        };
    }

    Err(EditTransactionError::CommitError(zx::Status::UNAVAILABLE))
}
