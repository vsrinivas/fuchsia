// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A tool to:
//! - acquire and display product bundle information (metadata)
//! - acquire related data files, such as disk partition images (data)

use {
    ::gcs::client::ProgressResponse,
    anyhow::{Context, Result},
    ffx_core::ffx_plugin,
    ffx_product_args::{GetCommand, ProductCommand, SubCommand},
    fidl_fuchsia_developer_ffx::RepositoryRegistryProxy,
    fidl_fuchsia_developer_ffx_ext::{RepositoryError, RepositorySpec},
    pbms::{get_product_data, update_metadata_all},
    std::{
        convert::TryInto,
        io::{stdout, Write},
    },
};

/// Provide functionality to show product-bundle metadata, fetch metadata, and
/// pull images and related data.
#[ffx_plugin("product.experimental", RepositoryRegistryProxy = "daemon::protocol")]
pub async fn product(cmd: ProductCommand, repos: RepositoryRegistryProxy) -> Result<()> {
    product_plugin_impl(cmd, &mut stdout(), repos).await
}

/// Dispatch to a sub-command.
pub async fn product_plugin_impl<W>(
    command: ProductCommand,
    writer: &mut W,
    repos: RepositoryRegistryProxy,
) -> Result<()>
where
    W: Write + Sync,
{
    match &command.sub {
        SubCommand::Get(cmd) => pb_get(writer, &cmd, repos).await?,
    }
    Ok(())
}

/// `ffx product get` sub-command.
async fn pb_get<W: Write + Sync>(
    writer: &mut W,
    cmd: &GetCommand,
    repos: RepositoryRegistryProxy,
) -> Result<()> {
    let base_dir = pbms::get_storage_dir().await?;
    if !cmd.cached {
        update_metadata_all(&base_dir, &mut |_d, _f| {
            write!(writer, ".")?;
            writer.flush()?;
            Ok(ProgressResponse::Continue)
        })
        .await?;
    }
    let product_url = pbms::select_product_bundle(&cmd.product_bundle_name).await?;
    get_product_data(&product_url, &base_dir, &mut |_d, _f| {
        write!(writer, ".")?;
        writer.flush()?;
        Ok(ProgressResponse::Continue)
    })
    .await?;

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

                writeln!(writer, "Created repository named '{}'", product_name)?;
            }
        }
    }

    Ok(())
}

#[cfg(test)]
mod test {}
