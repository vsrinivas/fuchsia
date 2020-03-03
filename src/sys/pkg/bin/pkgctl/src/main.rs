// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::args::{
        Args, Command, ExperimentCommand, ExperimentDisableCommand, ExperimentEnableCommand,
        ExperimentSubCommand, GcCommand, GetHashCommand, OpenCommand, PkgStatusCommand,
        RepoAddCommand, RepoCommand, RepoRemoveCommand, RepoSubCommand, ResolveCommand,
        RuleClearCommand, RuleCommand, RuleListCommand, RuleReplaceCommand, RuleReplaceFileCommand,
        RuleReplaceJsonCommand, RuleReplaceSubCommand, RuleSubCommand, UpdateCommand,
    },
    anyhow::{bail, format_err, Context as _},
    fidl_fuchsia_pkg::{
        PackageCacheMarker, PackageResolverAdminMarker, PackageResolverMarker, PackageUrl,
        RepositoryManagerMarker, RepositoryManagerProxy, UpdatePolicy,
    },
    fidl_fuchsia_pkg_ext::{BlobId, RepositoryConfig},
    fidl_fuchsia_pkg_rewrite::{EditTransactionProxy, EngineMarker, EngineProxy},
    fidl_fuchsia_pkg_rewrite_ext::{Rule as RewriteRule, RuleConfig},
    fidl_fuchsia_space::ManagerMarker as SpaceManagerMarker,
    fidl_fuchsia_update as fidl_update, files_async, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon as zx,
    std::{
        convert::{TryFrom, TryInto},
        fs::File,
        future::Future,
        io,
        process::exit,
    },
    thiserror::Error,
};

mod args;
mod error;

fn main() {
    let Args { command } = argh::from_env();
    let mut executor = match fasync::Executor::new().context("Error creating executor") {
        Ok(executor) => executor,
        Err(e) => {
            eprintln!("Error: {:?}", e);
            exit(1);
        }
    };
    exit(match executor.run_singlethreaded(main_helper(command)) {
        Ok(code) => code,
        Err(error) => {
            eprintln!("Error: {:?}", error);
            1
        }
    });
}

async fn main_helper(command: Command) -> Result<i32, anyhow::Error> {
    match command {
        Command::Resolve(ResolveCommand { pkg_url, selectors }) => {
            let resolver = connect_to_service::<PackageResolverMarker>()
                .context("Failed to connect to resolver service")?;
            println!("resolving {} with the selectors {:?}", pkg_url, selectors);

            let (dir, dir_server_end) = fidl::endpoints::create_proxy()?;

            let res = resolver
                .resolve(
                    &pkg_url,
                    &mut selectors.iter().map(|s| s.as_str()),
                    &mut UpdatePolicy { fetch_if_absent: true, allow_old_versions: true },
                    dir_server_end,
                )
                .await?;
            zx::Status::ok(res)?;

            let entries = files_async::readdir_recursive(&dir, /*timeout=*/ None).await?;
            println!("package contents:");
            for entry in entries {
                println!("/{}", entry.name);
            }

            Ok(0)
        }
        Command::GetHash(GetHashCommand { pkg_url }) => {
            let resolver = connect_to_service::<fidl_fuchsia_pkg::PackageResolverMarker>()
                .context("Failed to connect to resolver service")?;
            let blob_id =
                resolver.get_hash(&mut PackageUrl { url: pkg_url }).await?.map_err(|i| {
                    format_err!(
                        "Failed to get package hash with error: {}",
                        zx::Status::from_raw(i)
                    )
                })?;
            print!("{}", BlobId::from(blob_id));
            Ok(0)
        }
        Command::PkgStatus(PkgStatusCommand { pkg_url }) => {
            let resolver = connect_to_service::<fidl_fuchsia_pkg::PackageResolverMarker>()
                .context("Failed to connect to resolver service")?;
            let mut blob_id = match resolver.get_hash(&mut PackageUrl { url: pkg_url }).await? {
                Ok(blob_id) => blob_id,
                Err(status) => match zx::Status::from_raw(status) {
                    zx::Status::NOT_FOUND => {
                        println!("Package in registered TUF repo: no");
                        println!("Package on disk: unknown (did not check since not in tuf repo)");
                        return Ok(3);
                    }
                    other_failure_status => {
                        bail!("Cannot determine pkg status. Failed fuchsia.pkg.PackageResolver.GetHash with unexpected status: {:?}",
                          other_failure_status
                          );
                    }
                },
            };
            println!("Package in registered TUF repo: yes (merkle={})", BlobId::from(blob_id));

            let cache = connect_to_service::<PackageCacheMarker>()
                .context("Failed to connect to cache service")?;
            let (_, dir_server_end) = fidl::endpoints::create_proxy()?;
            let res = cache.open(&mut blob_id, &mut vec![].into_iter(), dir_server_end).await?;
            match zx::Status::ok(res) {
                Ok(_) => {}
                Err(zx::Status::NOT_FOUND) => {
                    println!("Package on disk: no");
                    return Ok(2);
                }
                Err(other_failure_status) => {
                    bail!("Cannot determine pkg status. Failed fuchsia.pkg.PackageCache.Open with unexpected status: {:?}",
                      other_failure_status
                          );
                }
            };
            println!(
                "Package on disk: yes (path={})",
                format!("/pkgfs/versions/{}", BlobId::from(blob_id))
            );
            Ok(0)
        }
        Command::Open(OpenCommand { meta_far_blob_id, selectors }) => {
            let cache = connect_to_service::<PackageCacheMarker>()
                .context("Failed to connect to cache service")?;
            println!("opening {} with the selectors {:?}", meta_far_blob_id, selectors);

            let (dir, dir_server_end) = fidl::endpoints::create_proxy()?;

            let res = cache
                .open(
                    &mut meta_far_blob_id.into(),
                    &mut selectors.iter().map(|s| s.as_str()),
                    dir_server_end,
                )
                .await?;
            zx::Status::ok(res)?;

            let entries = files_async::readdir_recursive(&dir, /*timeout=*/ None).await?;
            println!("package contents:");
            for entry in entries {
                println!("/{}", entry.name);
            }

            Ok(0)
        }
        Command::Repo(RepoCommand { verbose, subcommand }) => {
            let repo_manager = connect_to_service::<RepositoryManagerMarker>()
                .context("Failed to connect to resolver service")?;

            match subcommand {
                None => {
                    if !verbose {
                        // with no arguments, list available repos
                        let repos = fetch_repos(repo_manager).await?;

                        let mut urls =
                            repos.into_iter().map(|r| r.repo_url().to_string()).collect::<Vec<_>>();
                        urls.sort_unstable();
                        urls.into_iter().for_each(|url| println!("{}", url));
                    } else {
                        let repos = fetch_repos(repo_manager).await?;

                        let s = serde_json::to_string_pretty(&repos).expect("valid json");
                        println!("{}", s);
                    }
                    Ok(0)
                }
                Some(RepoSubCommand::Add(RepoAddCommand { file })) => {
                    let repo: RepositoryConfig =
                        serde_json::from_reader(io::BufReader::new(File::open(file)?))?;

                    let res = repo_manager.add(repo.into()).await?;
                    zx::Status::ok(res)?;

                    Ok(0)
                }

                Some(RepoSubCommand::Remove(RepoRemoveCommand { repo_url })) => {
                    let res = repo_manager.remove(&repo_url).await?;
                    zx::Status::ok(res)?;

                    Ok(0)
                }
            }
        }
        Command::Rule(RuleCommand { subcommand }) => {
            let engine = connect_to_service::<EngineMarker>()
                .context("Failed to connect to rewrite engine service")?;

            match subcommand {
                RuleSubCommand::List(RuleListCommand {}) => {
                    let (iter, iter_server_end) = fidl::endpoints::create_proxy()?;
                    engine.list(iter_server_end)?;

                    let mut rules = Vec::new();
                    loop {
                        let more = iter.next().await?;
                        if more.is_empty() {
                            break;
                        }
                        rules.extend(more);
                    }
                    let rules = rules.into_iter().map(|rule| rule.try_into()).collect::<Result<
                        Vec<RewriteRule>,
                        _,
                    >>(
                    )?;

                    for rule in rules {
                        println!("{:#?}", rule);
                    }
                }
                RuleSubCommand::Clear(RuleClearCommand {}) => {
                    do_transaction(engine, |transaction| async move {
                        transaction.reset_all()?;
                        Ok(transaction)
                    })
                    .await?;
                }
                RuleSubCommand::Replace(RuleReplaceCommand { subcommand }) => {
                    let RuleConfig::Version1(ref rules) = match subcommand {
                        RuleReplaceSubCommand::File(RuleReplaceFileCommand { file }) => {
                            serde_json::from_reader(io::BufReader::new(File::open(file)?))?
                        }
                        RuleReplaceSubCommand::Json(RuleReplaceJsonCommand { config }) => config,
                    };

                    do_transaction(engine, |transaction| {
                        async move {
                            transaction.reset_all()?;
                            // add() inserts rules as highest priority, so iterate over our
                            // prioritized list of rules so they end up in the right order.
                            for rule in rules.iter().rev() {
                                transaction.add(&mut rule.clone().into()).await?;
                            }
                            Ok(transaction)
                        }
                    })
                    .await?;
                }
            }

            Ok(0)
        }
        Command::Experiment(ExperimentCommand { subcommand }) => {
            let admin = connect_to_service::<PackageResolverAdminMarker>()
                .context("Failed to connect to package resolver admin service")?;

            match subcommand {
                ExperimentSubCommand::Enable(ExperimentEnableCommand { experiment }) => {
                    admin.set_experiment_state(experiment, true).await?;
                }
                ExperimentSubCommand::Disable(ExperimentDisableCommand { experiment }) => {
                    admin.set_experiment_state(experiment, false).await?;
                }
            }

            Ok(0)
        }
        Command::Update(UpdateCommand {}) => {
            let update = connect_to_service::<fidl_update::ManagerMarker>()
                .context("Failed to connect to update manager service")?;
            match update
                .check_now(
                    fidl_update::Options { initiator: Some(fidl_update::Initiator::User) },
                    None,
                )
                .await?
            {
                fidl_update::CheckStartedResult::Throttled => {
                    Err(format_err!("Update check was throttled.").into())
                }
                fidl_update::CheckStartedResult::Started
                | fidl_update::CheckStartedResult::InProgress => Ok(0),
            }
        }
        Command::Gc(GcCommand {}) => {
            let space_manager = connect_to_service::<SpaceManagerMarker>()
                .context("Failed to connect to space manager service")?;
            space_manager
                .gc()
                .await?
                .map_err(|err| {
                    format_err!("Garbage collection failed with error: {:?}", err).into()
                })
                .map(|_| 0i32)
        }
    }
}

#[derive(Debug, Error)]
enum EditTransactionError {
    #[error("internal fidl error: {}", _0)]
    Fidl(fidl::Error),

    #[error("commit error: {}", _0)]
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

        let transaction = cb(transaction).await?;

        let status = transaction.commit().await?;

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

async fn fetch_repos(
    repo_manager: RepositoryManagerProxy,
) -> Result<Vec<RepositoryConfig>, anyhow::Error> {
    let (iter, server_end) = fidl::endpoints::create_proxy()?;
    repo_manager.list(server_end)?;
    let mut repos = vec![];

    loop {
        let chunk = iter.next().await?;
        if chunk.is_empty() {
            break;
        }
        repos.extend(chunk);
    }

    repos
        .into_iter()
        .map(|repo| RepositoryConfig::try_from(repo).map_err(|e| anyhow::Error::from(e)))
        .collect()
}
