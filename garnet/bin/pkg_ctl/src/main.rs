// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![deny(warnings)]

use failure::{Error, ResultExt};
use fidl_fuchsia_pkg::{
    PackageCacheMarker, PackageResolverMarker, RepositoryManagerMarker, UpdatePolicy,
};
use fidl_fuchsia_pkg_ext::{BlobId, RepositoryConfig};
use files_async;
use fuchsia_async as fasync;
use fuchsia_component::client::{launch, launcher};
use fuchsia_zircon as zx;
use serde_json;
use std::convert::TryFrom;
use structopt::StructOpt;

#[derive(StructOpt)]
#[structopt(name = "pkgctl")]
struct Options {
    #[structopt(
        long = "pkg-resolver-uri",
        help = "Package URI of the package resolver",
        default_value = "fuchsia-pkg://fuchsia.com/pkg_resolver#meta/pkg_resolver.cmx"
    )]
    pkg_resolver_uri: String,

    #[structopt(
        long = "pkg-cache-uri",
        help = "Package URI of the package cache",
        default_value = "fuchsia-pkg://fuchsia.com/pkg_cache#meta/pkg_cache.cmx"
    )]
    pkg_cache_uri: String,

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
}

#[derive(StructOpt)]
enum RepoCommand {
    #[structopt(name = "list", about = "list repositories")]
    List,
}

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("Error creating executor")?;

    let Options { pkg_resolver_uri, pkg_cache_uri, cmd } = Options::from_args();

    // Launch the server and connect to the resolver service.
    let launcher = launcher().context("Failed to open launcher service")?;

    let fut = async {
        match cmd {
            Command::Resolve { pkg_uri, selectors } => {
                let app = launch(&launcher, pkg_resolver_uri, None)
                    .context("Failed to launch resolver service")?;
                let resolver = app
                    .connect_to_service(PackageResolverMarker)
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
                let app = launch(&launcher, pkg_cache_uri, None)
                    .context("Failed to launch cache service")?;
                let cache = app
                    .connect_to_service(PackageCacheMarker)
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
                let app = launch(&launcher, pkg_resolver_uri, None)
                    .context("Failed to launch resolver service")?;
                let repo_manager = app
                    .connect_to_service(RepositoryManagerMarker)
                    .context("Failed to connect to resolver service")?;

                match cmd {
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
        }
    };

    executor.run_singlethreaded(fut)
}
