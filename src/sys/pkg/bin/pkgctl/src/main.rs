// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::args::{
        Args, Command, ExperimentCommand, ExperimentDisableCommand, ExperimentEnableCommand,
        ExperimentSubCommand, GcCommand, GetHashCommand, OpenCommand, PkgStatusCommand,
        RepoAddCommand, RepoAddFileCommand, RepoAddSubCommand, RepoAddUrlCommand, RepoCommand,
        RepoConfigFormat, RepoRemoveCommand, RepoSubCommand, ResolveCommand, RuleClearCommand,
        RuleCommand, RuleDumpDynamicCommand, RuleListCommand, RuleReplaceCommand,
        RuleReplaceFileCommand, RuleReplaceJsonCommand, RuleReplaceSubCommand, RuleSubCommand,
    },
    crate::v1repoconf::{validate_host, SourceConfig},
    anyhow::{bail, format_err, Context as _},
    fidl_fuchsia_net_http::{self as http},
    fidl_fuchsia_pkg::{
        PackageCacheMarker, PackageResolverAdminMarker, PackageResolverMarker, PackageUrl,
        RepositoryManagerMarker, RepositoryManagerProxy,
    },
    fidl_fuchsia_pkg_ext::{
        BlobId, RepositoryConfig, RepositoryConfigBuilder, RepositoryStorageType,
    },
    fidl_fuchsia_pkg_rewrite::{EditTransactionProxy, EngineMarker, EngineProxy},
    fidl_fuchsia_pkg_rewrite_ext::{Rule as RewriteRule, RuleConfig},
    fidl_fuchsia_space::ManagerMarker as SpaceManagerMarker,
    files_async, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_url::pkg_url::RepoUrl,
    fuchsia_zircon as zx,
    futures::io::copy,
    futures::stream::TryStreamExt,
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
mod v1repoconf;

pub fn main() -> Result<(), anyhow::Error> {
    let mut executor = fasync::LocalExecutor::new()?;
    let Args { command } = argh::from_env();
    exit(executor.run_singlethreaded(main_helper(command))?)
}

async fn main_helper(command: Command) -> Result<i32, anyhow::Error> {
    match command {
        Command::Resolve(ResolveCommand { pkg_url, selectors }) => {
            let resolver = connect_to_protocol::<PackageResolverMarker>()
                .context("Failed to connect to resolver service")?;
            println!("resolving {} with the selectors {:?}", pkg_url, selectors);

            let (dir, dir_server_end) = fidl::endpoints::create_proxy()?;

            let () = resolver
                .resolve(&pkg_url, &mut selectors.iter().map(|s| s.as_str()), dir_server_end)
                .await?
                .map_err(fidl_fuchsia_pkg_ext::ResolveError::from)
                .with_context(|| format!("Failed to resolve {}", pkg_url))?;

            println!("package contents:");
            let mut stream = files_async::readdir_recursive(&dir, /*timeout=*/ None);
            while let Some(entry) = stream.try_next().await? {
                println!("/{}", entry.name);
            }

            Ok(0)
        }
        Command::GetHash(GetHashCommand { pkg_url }) => {
            let resolver = connect_to_protocol::<fidl_fuchsia_pkg::PackageResolverMarker>()
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
            let resolver = connect_to_protocol::<fidl_fuchsia_pkg::PackageResolverMarker>()
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

            let cache = connect_to_protocol::<PackageCacheMarker>()
                .context("Failed to connect to cache service")?;
            let (_, dir_server_end) = fidl::endpoints::create_proxy()?;
            let res = cache.open(&mut blob_id, &mut vec![].into_iter(), dir_server_end).await?;
            match res.map_err(zx::Status::from_raw) {
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
            let cache = connect_to_protocol::<PackageCacheMarker>()
                .context("Failed to connect to cache service")?;
            println!("opening {} with the selectors {:?}", meta_far_blob_id, selectors);

            let (dir, dir_server_end) = fidl::endpoints::create_proxy()?;

            let () = cache
                .open(
                    &mut meta_far_blob_id.into(),
                    &mut selectors.iter().map(|s| s.as_str()),
                    dir_server_end,
                )
                .await?
                .map_err(zx::Status::from_raw)?;

            let entries = files_async::readdir_recursive(&dir, /*timeout=*/ None)
                .try_collect::<Vec<_>>()
                .await?;
            println!("package contents:");
            for entry in entries {
                println!("/{}", entry.name);
            }

            Ok(0)
        }
        Command::Repo(RepoCommand { verbose, subcommand }) => {
            let repo_manager = connect_to_protocol::<RepositoryManagerMarker>()
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
                Some(RepoSubCommand::Add(RepoAddCommand { subcommand })) => {
                    match subcommand {
                        RepoAddSubCommand::File(RepoAddFileCommand {
                            persist,
                            format,
                            name,
                            file,
                        }) => {
                            let res = match format {
                                RepoConfigFormat::Version1 => {
                                    let mut repo: SourceConfig = serde_json::from_reader(
                                        io::BufReader::new(File::open(file)?),
                                    )?;
                                    // If a name is specified via the command line, override the
                                    // automatically derived name.
                                    if let Some(n) = name {
                                        repo.set_id(&n);
                                        validate_host(&repo.get_id())?;
                                    }
                                    if persist {
                                        repo.set_repo_storage_type(
                                            RepositoryStorageType::Persistent,
                                        );
                                    }
                                    let r = repo_manager.add(repo.into()).await?;
                                    r
                                }
                                RepoConfigFormat::Version2 => {
                                    let mut repo: RepositoryConfig = serde_json::from_reader(
                                        io::BufReader::new(File::open(file)?),
                                    )?;
                                    // If a name is specified via the command line, override the
                                    // automatically derived name.
                                    if let Some(n) = name {
                                        repo = RepositoryConfigBuilder::from(repo)
                                            .repo_url(RepoUrl::new(n)?)
                                            .build();
                                    }
                                    // The storage type can be overridden to persistent via the
                                    // command line.
                                    if persist {
                                        repo = RepositoryConfigBuilder::from(repo)
                                            .repo_storage_type(RepositoryStorageType::Persistent)
                                            .build();
                                    }
                                    let r = repo_manager.add(repo.into()).await?;
                                    r
                                }
                            };

                            let () = res.map_err(zx::Status::from_raw)?;
                        }
                        RepoAddSubCommand::Url(RepoAddUrlCommand {
                            persist,
                            format,
                            name,
                            repo_url,
                        }) => {
                            let res = fetch_url(repo_url).await?;
                            let res = match format {
                                RepoConfigFormat::Version1 => {
                                    let mut repo: SourceConfig = serde_json::from_slice(&res)?;
                                    // If a name is specified via the command line, override the
                                    // automatically derived name.
                                    if let Some(n) = name {
                                        repo.set_id(&n);
                                        validate_host(&repo.get_id())?;
                                    }
                                    if persist {
                                        repo.set_repo_storage_type(
                                            RepositoryStorageType::Persistent,
                                        );
                                    }
                                    let r = repo_manager.add(repo.into()).await?;
                                    r
                                }
                                RepoConfigFormat::Version2 => {
                                    let mut repo: RepositoryConfig = serde_json::from_slice(&res)?;
                                    // If a name is specified via the command line, override the
                                    // automatically derived name.
                                    if let Some(n) = name {
                                        repo = RepositoryConfigBuilder::from(repo)
                                            .repo_url(RepoUrl::new(n)?)
                                            .build();
                                    }
                                    // The storage type can be overridden to persistent via the
                                    // command line.
                                    if persist {
                                        repo = RepositoryConfigBuilder::from(repo)
                                            .repo_storage_type(RepositoryStorageType::Persistent)
                                            .build();
                                    }
                                    let r = repo_manager.add(repo.into()).await?;
                                    r
                                }
                            };
                            let () = res.map_err(zx::Status::from_raw)?;
                        }
                    }

                    Ok(0)
                }

                Some(RepoSubCommand::Remove(RepoRemoveCommand { repo_url })) => {
                    let res = repo_manager.remove(&repo_url).await?;
                    let () = res.map_err(zx::Status::from_raw)?;

                    Ok(0)
                }
            }
        }
        Command::Rule(RuleCommand { subcommand }) => {
            let engine = connect_to_protocol::<EngineMarker>()
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
                        transaction.reset_all().map_err(EditTransactionError::Fidl)?;
                        Ok(transaction)
                    })
                    .await?;
                }
                RuleSubCommand::DumpDynamic(RuleDumpDynamicCommand {}) => {
                    let (transaction, transaction_server_end) = fidl::endpoints::create_proxy()?;
                    let () = engine.start_edit_transaction(transaction_server_end)?;
                    let (iter, iter_server_end) = fidl::endpoints::create_proxy()?;
                    transaction.list_dynamic(iter_server_end)?;
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
                    let rule_configs = RuleConfig::Version1(rules);
                    let dynamic_rules = serde_json::to_string_pretty(&rule_configs)?;
                    println!("{}", dynamic_rules);
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
                            transaction.reset_all().map_err(EditTransactionError::Fidl)?;
                            // add() inserts rules as highest priority, so iterate over our
                            // prioritized list of rules so they end up in the right order.
                            for rule in rules.iter().rev() {
                                let () = transaction
                                    .add(&mut rule.clone().into())
                                    .await
                                    .map_err(EditTransactionError::Fidl)?
                                    .map_err(zx::Status::from_raw)
                                    .map_err(EditTransactionError::AddError)?;
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
            let admin = connect_to_protocol::<PackageResolverAdminMarker>()
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
        Command::Gc(GcCommand {}) => {
            let space_manager = connect_to_protocol::<SpaceManagerMarker>()
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
    #[error("internal fidl error")]
    Fidl(#[source] fidl::Error),

    #[error("commit error")]
    CommitError(#[source] zx::Status),

    #[error("add error")]
    AddError(#[source] zx::Status),
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
    R: Future<Output = Result<EditTransactionProxy, EditTransactionError>>,
{
    // Make a reasonable effort to retry the edit after a concurrent edit, but don't retry forever.
    for _ in 0..100 {
        let (transaction, transaction_server_end) =
            fidl::endpoints::create_proxy().map_err(EditTransactionError::Fidl)?;
        let () = engine
            .start_edit_transaction(transaction_server_end)
            .map_err(EditTransactionError::Fidl)?;

        let transaction = cb(transaction).await?;

        let response = transaction.commit().await.map_err(EditTransactionError::Fidl)?;

        // Retry edit transaction on concurrent edit
        return match response.map_err(zx::Status::from_raw) {
            Ok(()) => Ok(()),
            Err(zx::Status::UNAVAILABLE) => {
                continue;
            }
            Err(status) => Err(EditTransactionError::CommitError(status)),
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

async fn fetch_url<T: Into<String>>(url_string: T) -> Result<Vec<u8>, anyhow::Error> {
    let http_svc = connect_to_protocol::<http::LoaderMarker>()
        .context("Unable to connect to fuchsia.net.http.Loader")?;

    let url_request = http::Request {
        url: Some(url_string.into()),
        method: Some(String::from("GET")),
        headers: None,
        body: None,
        deadline: None,
        ..http::Request::EMPTY
    };

    let response =
        http_svc.fetch(url_request).await.context("Error while calling Loader::Fetch")?;

    if let Some(e) = response.error {
        return Err(format_err!("LoaderProxy error - {:?}", e));
    }

    let socket = match response.body {
        Some(s) => fasync::Socket::from_socket(s).context("Error while wrapping body socket")?,
        _ => {
            return Err(format_err!("failed to read UrlBody from the stream"));
        }
    };

    let mut body = Vec::new();
    let bytes_received =
        copy(socket, &mut body).await.context("Failed to read bytes from the socket")?;

    if bytes_received < 1 {
        return Err(format_err!(
            "Failed to download data from url! bytes_received = {}",
            bytes_received
        ));
    }

    Ok(body)
}
