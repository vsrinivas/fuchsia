// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A tool to:
//! - acquire and display product bundle information (metadata)
//! - acquire related data files, such as disk partition images (data)

use {
    anyhow::{bail, Context, Result},
    ffx_config::ConfigLevel,
    ffx_core::ffx_plugin,
    ffx_product_bundle_args::{
        CreateCommand, GetCommand, ListCommand, ProductBundleCommand, RemoveCommand, SubCommand,
    },
    ffx_writer::Writer,
    fidl,
    fidl_fuchsia_developer_ffx::{RepositoryIteratorMarker, RepositoryRegistryProxy},
    fidl_fuchsia_developer_ffx_ext::{RepositoryConfig, RepositoryError, RepositorySpec},
    fuchsia_url::RepositoryUrl,
    pbms::{
        get_images_dir, get_product_data, is_locally_built, is_pb_ready, product_bundle_urls,
        select_auth, select_product_bundle, update_metadata_all, ListingMode,
    },
    std::{
        convert::TryInto,
        fs::{read_dir, remove_dir_all},
        io::{stderr, stdin},
        path::Component,
    },
    structured_ui::{self, Presentation, Response, SimplePresentation, TableRows, TextUi},
    url::Url,
};

mod create;

const CONFIG_METADATA: &str = "pbms.metadata";

/// Provide functionality to list product-bundle metadata, fetch metadata, and
/// pull images and related data.
#[ffx_plugin(RepositoryRegistryProxy = "daemon::protocol")]
pub async fn product_bundle(
    cmd: ProductBundleCommand,
    writer: Writer,
    repos: RepositoryRegistryProxy,
) -> Result<()> {
    let mut input = stdin();
    let mut output = writer;
    let mut err_out = stderr();
    let mut ui = TextUi::new(&mut input, &mut output, &mut err_out);
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
        SubCommand::List(cmd) => pb_list(ui, &cmd).await,
        SubCommand::Get(cmd) => pb_get(ui, &cmd, repos).await,
        SubCommand::Create(cmd) => pb_create(&cmd).await,
        SubCommand::Remove(cmd) => pb_remove(ui, &cmd, repos).await,
    }
}

async fn check_for_custom_metadata<I>(ui: &mut I, title: &str)
where
    I: structured_ui::Interface + Sync,
{
    let mut note = TableRows::builder();
    note.title(title);
    match ffx_config::query(CONFIG_METADATA)
        .level(Some(ConfigLevel::User))
        .get::<Vec<String>>()
        .await
    {
        Ok(v) => {
            if !v.is_empty() {
                note.note(
                    "\nIt looks like you have a custom search path in your FFX configuration, \n\
                    which may prevent the tool from finding product bundles. Try:\n\n    \
                        ffx config get pbms.metadata \n\n\
                    to review your current configuration.\n",
                );
            }
        }
        // If the config doesn't return an array, we assume there's no custom config set and bail.
        Err(_) => (),
    }
    ui.present(&Presentation::Table(note)).expect("Problem presenting the custom metadata note.");
}

/// `ffx product-bundle remove` sub-command.
async fn pb_remove<I>(ui: &mut I, cmd: &RemoveCommand, repos: RepositoryRegistryProxy) -> Result<()>
where
    I: structured_ui::Interface + Sync,
{
    tracing::debug!("pb_remove");
    let mut pbs_to_remove = Vec::new();
    if cmd.all {
        let entries = product_bundle_urls().await.context("list pbms")?;
        for url in entries {
            if !is_locally_built(&url) && is_pb_ready(&url).await? {
                pbs_to_remove.push(url);
            }
        }
    } else {
        // We use the default matching functionality.
        let should_print = true;
        match select_product_bundle(
            &cmd.product_bundle_name,
            ListingMode::RemovableBundles,
            should_print,
        )
        .await
        {
            Ok(url) => {
                if !is_locally_built(&url) && is_pb_ready(&url).await? {
                    pbs_to_remove.push(url);
                }
            }
            Err(e) => {
                println!("Couldn't determine which bundle to remove: {:?}", e);
                return Ok(());
            }
        }
    }
    if pbs_to_remove.len() > 0 {
        pb_remove_all(ui, pbs_to_remove, cmd.force, repos).await
    } else {
        // Nothing to remove.
        check_for_custom_metadata(ui, "There are no product bundles to remove.").await;
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
async fn pb_remove_all<I>(
    ui: &mut I,
    pbs_to_remove: Vec<Url>,
    force: bool,
    repos: RepositoryRegistryProxy,
) -> Result<()>
where
    I: structured_ui::Interface + Sync,
{
    let mut confirmation = true;
    if !force {
        let mut table = TableRows::builder();
        table.title(format!(
            "This will delete the following {} product bundle(s):",
            pbs_to_remove.len()
        ));
        for url in &pbs_to_remove {
            table.row(vec![format!("    {}", url)]);
        }
        ui.present(&Presentation::Table(table))?;

        let mut question = SimplePresentation::builder();
        question.prompt("Are you sure you wish to proceed? (y/n)");
        confirmation = match ui.present(&Presentation::StringPrompt(question))? {
            Response::Choice(str) => {
                let response = str.to_lowercase();
                let response = response.trim_end();
                response == "y" || response == "yes"
            }
            _ => false,
        };
    }
    if confirmation {
        let all_repos = get_repos(&repos).await?;
        for url in pbs_to_remove {
            // Resolve the directory for the target bundle.
            let root_dir = pbms::get_product_dir(&url).await.context("Couldn't get directory")?;
            assert_ne!(root_dir.components().count(), 0, "An empty PBMS path is not allowed.");
            assert_ne!(
                root_dir.components().collect::<Vec<_>>(),
                &[Component::RootDir],
                "Refusing to delete from the root of the filesystem."
            );
            assert!(
                !root_dir.components().into_iter().any(|x| x == Component::ParentDir),
                "Directory traversal is not allowed."
            );
            let name = url.fragment().expect("URL with trailing product_name fragment.");

            // If there is a repository for the bundle...
            let repo_name = name.replace('_', "-");
            if let Ok(repo_path) = pbms::get_packages_dir(&url).await {
                if all_repos.iter().any(|r| {
                    // The name has to match...
                    r.name == repo_name &&
                    // It has to be a Pm-style repo, since that's what we add in `get`...
                    match &r.spec {
                        // And the local path has to match, to make sure it's the right bundle of
                        // that name...
                        RepositorySpec::Pm { path } => {
                            path.clone().into_std_path_buf() == repo_path
                        }
                        _ => false,
                    }
                }) {
                    // If all those match, we remove it.
                    repos
                        .remove_repository(&repo_name)
                        .await
                        .context("communicating with ffx daemon")?;
                    tracing::info!("Removed repository named '{}'", repo_name);
                }
            }

            // Delete the bundle directory.
            let product_dir = root_dir.join(name);
            println!("Removing product bundle '{}'.", url);
            tracing::debug!("Removing product bundle '{}'.", url);
            remove_dir_all(&product_dir).context("removing product directory")?;

            // If there are no more bundles in this directory, delete it too.
            if !read_dir(&root_dir).context("reading root dir)")?.any(|d| {
                d.expect("intermittent IO error, please try the command again.").path().is_dir()
            }) {
                remove_dir_all(&root_dir).context("removing root directory")?;
            }
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
        update_metadata_all(&storage_dir, select_auth(cmd.oob_auth, cmd.auth), ui).await?;
    }
    let mut entries = product_bundle_urls().await.context("list pbms")?;
    if entries.is_empty() {
        check_for_custom_metadata(ui, "No product bundles found.").await;
        return Ok(());
    }
    entries.sort();
    entries.reverse();
    let mut table = TableRows::builder();
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
    ui.present(&Presentation::Table(table.clone()))?;
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

    let path = get_images_dir(&product_url).await;
    if path.is_ok() && path.unwrap().exists() && !cmd.force {
        let mut note = TableRows::builder();
        note.title("This product bundle is already downloaded. Use --force to replace it.");
        ui.present(&Presentation::Table(note)).expect("Problem presenting the note.");
        return Ok(());
    }

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

    // If a repo with the selected name already exists, that's an error.
    let repo_list = get_repos(&repos).await?;
    if repo_list.iter().any(|r| r.name == repo_name) {
        let mut note = TableRows::builder();
        note.title(format!(
            "A package repository already exists with the name '{}'. \
            Specify an alternative name using the --repository flag.",
            repo_name
        ));
        ui.present(&Presentation::Table(note)).expect("Problem presenting the note.");
        return Ok(());
    }

    // Go ahead and download the product images.
    if !get_product_data(
        &product_url,
        &output_dir,
        select_auth(cmd.oob_auth, cmd.auth),
        ui,
        cmd.force,
    )
    .await?
    {
        return Ok(());
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
        update_metadata_all(&base_dir, select_auth(cmd.oob_auth, cmd.auth), ui).await?;
    }
    let should_print = true;
    select_product_bundle(&cmd.product_bundle_name, ListingMode::AllBundles, should_print).await
}

/// `ffx product-bundle create` sub-command.
async fn pb_create(cmd: &CreateCommand) -> Result<()> {
    create::create_product_bundle(cmd).await
}

#[cfg(test)]
mod test {}
