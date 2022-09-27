// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A tool to:
//! - acquire and display product bundle information (metadata)
//! - acquire related data files, such as disk partition images (data)

use {
    anyhow::{bail, Context, Result},
    ffx_core::ffx_plugin,
    ffx_product_bundle_args::{
        CreateCommand, GetCommand, ListCommand, ProductBundleCommand, SubCommand,
    },
    fidl_fuchsia_developer_ffx::RepositoryRegistryProxy,
    fidl_fuchsia_developer_ffx_ext::{RepositoryError, RepositorySpec},
    fuchsia_url::RepositoryUrl,
    pbms::{
        get_product_data, is_pb_ready, product_bundle_urls, structured_ui, update_metadata_all,
    },
    std::{
        convert::TryInto,
        io::{stderr, stdin, stdout},
    },
};

mod create;

/// Provide functionality to list product-bundle metadata, fetch metadata, and
/// pull images and related data.
#[ffx_plugin(RepositoryRegistryProxy = "daemon::protocol")]
pub async fn product_bundle(
    cmd: ProductBundleCommand,
    repos: RepositoryRegistryProxy,
) -> Result<()> {
    let mut input = stdin();
    let mut output = stdout();
    let mut err_out = stderr();
    let mut ui = structured_ui::TextUi::new(&mut input, &mut output, &mut err_out);
    product_bundle_plugin_impl(cmd, &mut ui, repos).await
}

/// Dispatch to a sub-command.
pub async fn product_bundle_plugin_impl<I>(
    command: ProductBundleCommand,
    ui: &mut I,
    repos: RepositoryRegistryProxy,
) -> Result<()>
where
    I: structured_ui::Interface + Sync,
{
    match &command.sub {
        SubCommand::List(cmd) => pb_list(ui, &cmd).await?,
        SubCommand::Get(cmd) => pb_get(ui, &cmd, repos).await?,
        SubCommand::Create(cmd) => pb_create(&cmd).await?,
    }
    Ok(())
}

/// `ffx product-bundle list` sub-command.
async fn pb_list<I>(ui: &mut I, cmd: &ListCommand) -> Result<()>
where
    I: structured_ui::Interface + Sync,
{
    tracing::debug!("pb_list");
    if !cmd.cached {
        let storage_dir = pbms::get_storage_dir().await?;
        update_metadata_all(&storage_dir, ui).await?;
    }
    let mut entries = product_bundle_urls().await.context("list pbms")?;
    entries.sort();
    let mut table = structured_ui::TableRows::builder();
    for entry in entries {
        let ready = if is_pb_ready(&entry).await? { "*" } else { " " };
        table.row(vec![format!("{}", ready), format!("{}", entry)]);
    }
    table.note(
        "\
        \n*No need to fetch with `ffx product-bundle get ...`. \
        The '*' is not part of the name.\
        \n",
    );
    ui.present(&structured_ui::Presentation::Table(table.clone()))?;
    Ok(())
}

/// `ffx product-bundle get` sub-command.
async fn pb_get<I>(ui: &mut I, cmd: &GetCommand, repos: RepositoryRegistryProxy) -> Result<()>
where
    I: structured_ui::Interface + Sync,
{
    let start = std::time::Instant::now();
    tracing::debug!("pb_get {:?}", cmd.product_bundle_name);
    let product_url = determine_pbm_url(cmd, ui).await?;
    let output_dir = pbms::get_product_dir(&product_url).await?;
    get_product_data(&product_url, &output_dir, ui).await?;

    let repo_name = if let Some(repo_name) = &cmd.repository {
        repo_name.clone()
    } else if let Some(product_name) = product_url.fragment() {
        // FIXME(103661): Repository names must be a valid domain name, and cannot contain
        // '_'. We might be able to expand our support for [opaque hosts], which supports
        // arbitrary ASCII codepoints. Until then, replace any '_' with '-'.
        //
        // [opaque hosts]: https://url.spec.whatwg.org/#opaque-host
        let repo_name = product_name.replace('_', "-");

        if repo_name != product_name {
            tracing::info!(
                "Repository names cannot contain '_'. Replacing with '-' in {}",
                product_name
            );
        }

        repo_name
    } else {
        // Otherwise use the standard default.
        "devhost".to_string()
    };

    // Make sure the repository name is valid.
    if let Err(err) = RepositoryUrl::parse_host(repo_name.clone()) {
        bail!("invalid repository name {}: {}", repo_name, err);
    }

    // Register a repository with the daemon if we downloaded any packaging artifacts.
    if let Ok(repo_path) = pbms::get_packages_dir(&product_url).await {
        if repo_path.exists() {
            let repo_path = repo_path
                .canonicalize()
                .with_context(|| format!("canonicalizing {:?}", repo_path))?;
            let repo_spec = RepositorySpec::Pm { path: repo_path.try_into()? };
            repos
                .add_repository(&repo_name, &mut repo_spec.into())
                .await
                .context("communicating with ffx daemon")?
                .map_err(RepositoryError::from)
                .with_context(|| format!("registering repository {}", repo_name))?;

            tracing::info!("Created repository named '{}'", repo_name);
        }
    }

    tracing::debug!(
        "Total fx product-bundle get runtime {} seconds.",
        start.elapsed().as_secs_f32()
    );
    Ok(())
}

/// Convert cli args to a full URL pointing to product bundle metadata
async fn determine_pbm_url<I>(cmd: &GetCommand, ui: &mut I) -> Result<url::Url>
where
    I: structured_ui::Interface + Sync,
{
    if !cmd.cached {
        let base_dir = pbms::get_storage_dir().await?;
        update_metadata_all(&base_dir, ui).await?;
    }
    pbms::select_product_bundle(&cmd.product_bundle_name).await
}

/// `ffx product-bundle create` sub-command.
async fn pb_create(cmd: &CreateCommand) -> Result<()> {
    create::create_product_bundle(cmd).await
}

#[cfg(test)]
mod test {}
