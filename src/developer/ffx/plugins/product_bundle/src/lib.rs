// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A tool to:
//! - acquire and display product bundle information (metadata)
//! - acquire related data files, such as disk partition images (data)

use {
    ::gcs::client::{DirectoryProgress, FileProgress, ProgressResponse, ProgressResult},
    anyhow::{bail, Context, Result},
    ffx_core::ffx_plugin,
    ffx_product_bundle_args::{
        CreateCommand, GetCommand, ListCommand, ProductBundleCommand, SubCommand,
    },
    fidl_fuchsia_developer_ffx::RepositoryRegistryProxy,
    fidl_fuchsia_developer_ffx_ext::{RepositoryError, RepositorySpec},
    fuchsia_url::RepositoryUrl,
    pbms::{
        get_product_data, is_pb_ready, product_bundle_urls, update_metadata_all,
        update_metadata_from,
    },
    std::{
        convert::TryInto,
        io::{stdout, Write},
        path::PathBuf,
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
    tracing::debug!("pb_list");
    if !cmd.cached {
        let storage_dir = pbms::get_storage_dir().await?;
        update_metadata_all(&storage_dir, &mut |_d, _f| Ok(ProgressResponse::Continue)).await?;
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
    tracing::debug!("pb_get {:?}", cmd.product_bundle_name);
    let product_url = determine_pbm_url(cmd, &mut |_d, _f| {
        write!(writer, ".")?;
        writer.flush()?;
        Ok(ProgressResponse::Continue)
    })
    .await?;
    let output_dir = get_output_dir(&product_url, &cmd.out_dir).await?;
    get_product_data(&product_url, &output_dir, &mut |_d, _f| {
        write!(writer, ".")?;
        writer.flush()?;
        Ok(ProgressResponse::Continue)
    })
    .await?;

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
async fn determine_pbm_url<F>(cmd: &GetCommand, progress: &mut F) -> Result<url::Url>
where
    F: FnMut(DirectoryProgress<'_>, FileProgress<'_>) -> ProgressResult,
{
    // If an output directory is specified, use new entirely different methods
    // to determine the PB to use. Eventually the 'else' clause is expected to
    // be deprecated and removed.
    Ok(if let Some(dir) = &cmd.out_dir {
        if let Some(bundle_name) = &cmd.product_bundle_name {
            let product_url = url::Url::parse(&bundle_name).context("parsing product url")?;
            update_metadata_from(&product_url, dir, progress).await?;
            product_url
        } else {
            bail!("When using --out-dir, a product bundle url is required.");
        }
    } else {
        if !cmd.cached {
            let base_dir = pbms::get_storage_dir().await?;
            update_metadata_all(&base_dir, progress).await?;
        }
        pbms::select_product_bundle(&cmd.product_bundle_name).await?
    })
}

/// `ffx product-bundle create` sub-command.
async fn pb_create(cmd: &CreateCommand) -> Result<()> {
    create::create_product_bundle(cmd).await
}

/// Determine the output dir from the args.
async fn get_output_dir(product_url: &url::Url, output_dir: &Option<PathBuf>) -> Result<PathBuf> {
    let path = match output_dir {
        Some(d) => d.to_path_buf(),
        None => pbms::get_product_dir(product_url).await?,
    };
    tracing::debug!("get_output_dir {:?}", path);
    Ok(path)
}

#[cfg(test)]
mod test {}
