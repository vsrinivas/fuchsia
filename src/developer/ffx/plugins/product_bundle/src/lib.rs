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
        CreateCommand, GetCommand, ListCommand, ProductBundleCommand, RemoveCommand, SubCommand,
    },
    fidl,
    fidl_fuchsia_developer_ffx::{RepositoryIteratorMarker, RepositoryRegistryProxy},
    fidl_fuchsia_developer_ffx_ext::{RepositoryConfig, RepositoryError, RepositorySpec},
    fuchsia_url::RepositoryUrl,
    pbms::{
        get_product_data, is_pb_ready, product_bundle_urls, select_product_bundle, structured_ui,
        update_metadata_all, ListingMode,
    },
    std::{
        convert::TryInto,
        fs::remove_dir_all,
        io::{stderr, stdin, stdout, BufRead, Read, Write},
    },
    url::Url,
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
    product_bundle_plugin_impl(cmd, &mut stdin().lock(), &mut ui, repos).await
}

/// Dispatch to a sub-command.
pub async fn product_bundle_plugin_impl<R, I>(
    command: ProductBundleCommand,
    reader: &mut R,
    ui: &mut I,
    repos: RepositoryRegistryProxy,
) -> Result<()>
where
    R: Read + Sync + BufRead,
    I: structured_ui::Interface + Sync,
{
    match &command.sub {
        SubCommand::List(cmd) => pb_list(ui, &cmd).await?,
        SubCommand::Get(cmd) => pb_get(ui, &cmd, repos).await?,
        SubCommand::Create(cmd) => pb_create(&cmd).await?,
        SubCommand::Remove(cmd) => pb_remove(reader, &cmd, repos).await?,
    }
    Ok(())
}

/// `ffx product-bundle remove` sub-command.
async fn pb_remove<R>(
    reader: &mut R,
    cmd: &RemoveCommand,
    repos: RepositoryRegistryProxy,
) -> Result<()>
where
    R: Read + Sync + BufRead,
{
    tracing::debug!("pb_remove");
    let mut pbs_to_remove = Vec::new();
    if cmd.all {
        let entries = product_bundle_urls().await.context("list pbms")?;
        for url in entries {
            if url.scheme() != "file" && is_pb_ready(&url).await? {
                pbs_to_remove.push(url);
            }
        }
    } else {
        // We use the default matching functionality.
        let url = select_product_bundle(&cmd.product_bundle_name, ListingMode::ReadyBundlesOnly)
            .await
            .context("Problem retrieving product bundle information.")?;
        if url.scheme() != "file" && is_pb_ready(&url).await? {
            pbs_to_remove.push(url);
        }
    }
    if pbs_to_remove.len() > 0 {
        pb_remove_all(reader, pbs_to_remove, cmd.force, repos).await
    } else {
        // Nothing to remove.
        Ok(())
    }
}

async fn get_repos(repos_proxy: &RepositoryRegistryProxy) -> Result<Vec<RepositoryConfig>> {
    let (client, server) = fidl::endpoints::create_endpoints::<RepositoryIteratorMarker>()
        .context("creating endpoints")?;
    repos_proxy.list_repositories(server).context("listing repositories")?;
    let client = client.into_proxy().context("creating repository iterator proxy")?;

    let mut repos = vec![];
    loop {
        let batch = client.next().await.context("fetching next batch of repositories")?;
        if batch.is_empty() {
            break;
        }

        for repo in batch {
            repos.push(repo.try_into().context("converting repository config")?);
        }
    }

    repos.sort();
    Ok(repos)
}

/// Removes a set of product bundle directories, with user confirmation if "force" is false.
async fn pb_remove_all<R>(
    reader: &mut R,
    pbs_to_remove: Vec<Url>,
    force: bool,
    repos: RepositoryRegistryProxy,
) -> Result<()>
where
    R: Read + Sync + BufRead,
{
    let mut confirmation = true;
    if !force {
        let mut response = String::new();
        print!(
            "This will delete {} product bundle(s). Are you sure you wish to proceed? (y/n) ",
            pbs_to_remove.len()
        );
        stdout().flush().ok();
        reader.read_line(&mut response).context("reading input")?;
        confirmation = match response.to_lowercase().trim_end() {
            "y" | "yes" => true,
            _ => false,
        }
    }
    if confirmation {
        let all_repos = get_repos(&repos).await?;
        for url in pbs_to_remove {
            // Resolve the directory for the target bundle.
            let root_dir = pbms::get_product_dir(&url).await.context("Couldn't get directory")?;
            let name = url.fragment().expect("URL with trailing product_name fragment.");

            // If there is a repository for the bundle...
            let repo_name = name.replace('_', "-");
            if let Ok(repo_path) = pbms::get_packages_dir(&url).await {
                if all_repos.iter().any(|r| {
                    // The name has to match...
                    r.name == repo_name &&
                    // It has to be a Pm-style repo, since that's what we add in `get`...
                    match &r.spec {
                        // And the local path has to match, to make sure it's the right bundle of that name...
                        RepositorySpec::Pm { path } => path.clone().into_std_path_buf() == repo_path,
                        _ => false,
                    }
                }) {
                    // If all those match, we remove it.
                    repos.remove_repository(&repo_name).await.context("communicating with ffx daemon")?;
                    tracing::info!("Removed repository named '{}'", repo_name);
                }
            }

            // Delete the bundle directory.
            let product_dir = root_dir.join(name);
            println!("Removing product bundle '{}'.", url);
            tracing::debug!("Removing product bundle '{}'.", url);
            remove_dir_all(&product_dir).context("removing product directory")?;
        }
    } else {
        println!("Cancelling product bundle removal.");
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
    pbms::select_product_bundle(&cmd.product_bundle_name, ListingMode::AllBundles).await
}

/// `ffx product-bundle create` sub-command.
async fn pb_create(cmd: &CreateCommand) -> Result<()> {
    create::create_product_bundle(cmd).await
}

#[cfg(test)]
mod test {}
