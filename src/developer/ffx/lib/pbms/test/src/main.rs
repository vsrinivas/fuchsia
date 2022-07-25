// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A tool to test product bundle code.

use {
    anyhow::{Context as _, Result},
    args::{CreateCommand, GetCommand, ListCommand, ProductBundleCommand, SubCommand},
    pbms::{get_product_data, is_pb_ready, product_bundle_urls, update_metadata},
    std::{
        convert::TryInto,
        io::{stdout, Write},
        path::PathBuf,
    },
    thiserror::Error,
};

mod args;

#[derive(Debug, Error)]
enum RepositoryError {}

#[derive(Debug)]
enum RepositorySpec {
    Pm {
        #[allow(unused)]
        path: PathBuf,
    },
}

pub struct RepositoryRegistryProxy {}

impl RepositoryRegistryProxy {
    async fn add_repository(
        &self,
        _: &str,
        spec: &RepositorySpec,
    ) -> Result<Result<(), RepositoryError>> {
        println!("no-op add_repository(). {:?}", spec);
        Ok(Ok(()))
    }
}

/// Provide functionality to list product-bundle metadata, fetch metadata, and
/// pull images and related data.
#[fuchsia_async::run_singlethreaded]
pub async fn main() -> Result<()> {
    let cmd: ProductBundleCommand = argh::from_env();
    let repos = RepositoryRegistryProxy {};
    product_bundle_plugin_impl(cmd, &mut stdout(), repos).await
}

/// Dispatch to a sub-command.
pub async fn product_bundle_plugin_impl<W>(
    command: ProductBundleCommand,
    writer: &mut W,
    repos: RepositoryRegistryProxy,
) -> Result<()>
where
    W: Write + Sync,
{
    match &command.sub {
        SubCommand::List(cmd) => pb_list(writer, &cmd).await?,
        SubCommand::Get(cmd) => pb_get(writer, &cmd, repos).await?,
        SubCommand::Create(cmd) => pb_create(&cmd).await?,
    }
    Ok(())
}

/// `ffx product-bundle list` sub-command.
async fn pb_list<W: Write + Sync>(writer: &mut W, cmd: &ListCommand) -> Result<()> {
    if !cmd.cached {
        update_metadata(/*verbose=*/ false, writer).await?;
    }
    let mut entries = product_bundle_urls().await.context("list pbms")?;
    entries.sort();
    writeln!(writer, "")?;
    for entry in entries {
        let ready = if is_pb_ready(&entry).await? { "*" } else { "" };
        writeln!(writer, "{}{}", entry, ready)?;
    }
    writeln!(
        writer,
        "\
        \n*No need to fetch with `ffx product-bundle get ...`. \
        The '*' is not part of the name.\
        \n"
    )?;
    Ok(())
}

/// `ffx product-bundle get` sub-command.
async fn pb_get<W: Write + Sync>(
    writer: &mut W,
    cmd: &GetCommand,
    repos: RepositoryRegistryProxy,
) -> Result<()> {
    let start = std::time::Instant::now();
    if !cmd.cached {
        update_metadata(cmd.verbose, writer).await?;
    }
    let product_url = pbms::select_product_bundle(&cmd.product_bundle_name).await?;
    get_product_data(&product_url, cmd.verbose, writer).await?;

    // Register a repository with the daemon if we downloaded any packaging artifacts.
    if let Some(product_name) = product_url.fragment() {
        if let Ok(repo_path) = pbms::get_packages_dir(&product_url).await {
            if repo_path.exists() {
                let repo_path = repo_path
                    .canonicalize()
                    .with_context(|| format!("canonicalizing {:?}", repo_path))?;

                let repo_spec = RepositorySpec::Pm { path: repo_path.try_into()? };
                repos
                    .add_repository(&product_name, &mut repo_spec.into())
                    .await
                    .context("communicating with ffx daemon")?
                    .map_err(RepositoryError::from)
                    .with_context(|| format!("registering repository {}", product_name))?;

                tracing::info!("Created repository named '{}'", product_name);
            }
        }
    }

    tracing::debug!(
        "Total fx product-bundle get runtime {} seconds.",
        start.elapsed().as_secs_f32()
    );
    Ok(())
}

/// `ffx product-bundle create` sub-command.
async fn pb_create(_cmd: &CreateCommand) -> Result<()> {
    println!("The create command is not implemented in the test tool.");
    Ok(())
}

#[cfg(test)]
mod test {}
