// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A tool to:
//! - acquire and display product bundle information (metadata)
//! - acquire related data files, such as disk partition images (data)

use {
    anyhow::{Context, Result},
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_product_get_args::GetCommand,
    fidl_fuchsia_developer_ffx::RepositoryRegistryProxy,
    fidl_fuchsia_developer_ffx_ext::{RepositoryError, RepositorySpec},
    fuchsia_url::RepositoryUrl,
    pbms::{fetch_data_for_product_bundle_v1, update_metadata_from},
    sdk_metadata,
    std::{
        convert::TryInto,
        io::{stderr, stdin, stdout},
        path::Path,
    },
    structured_ui,
};

/// `ffx product get` sub-command.
#[ffx_plugin("product.experimental", RepositoryRegistryProxy = "daemon::protocol")]
pub async fn pb_get(cmd: GetCommand, repos: RepositoryRegistryProxy) -> Result<()> {
    let mut input = stdin();
    let mut output = stdout();
    let mut err_out = stderr();
    let mut ui = structured_ui::TextUi::new(&mut input, &mut output, &mut err_out);
    pb_get_impl(&cmd, repos, &mut ui).await
}

async fn pb_get_impl<I: structured_ui::Interface + Sync>(
    cmd: &GetCommand,
    repos: RepositoryRegistryProxy,
    ui: &mut I,
) -> Result<()> {
    let start = std::time::Instant::now();
    tracing::info!("---------------------- Begin ----------------------------");
    tracing::debug!("product_url Url::parse");
    let product_url = match url::Url::parse(&cmd.product_bundle_url) {
        Ok(p) => p,
        _ => ffx_bail!(
            "The source location must be a URL, failed to parse {:?}",
            cmd.product_bundle_url
        ),
    };
    let local_dir = &cmd.out_dir;
    make_way_for_output(&local_dir, cmd.force).await.context("make_way_for_output")?;
    let pbm_path = local_dir.join("product_bundle.json");
    tracing::debug!("first read_product_bundle_metadata {:?}", pbm_path);
    let product_bundle_metadata = match read_product_bundle_metadata(&pbm_path)
        .await
        .context("reading product metadata")
    {
        Ok(name) => name,
        _ => {
            // Try updating the metadata and then reading again.
            tracing::debug!("update_metadata_from {:?} {:?}", product_url, local_dir);
            update_metadata_from(&product_url, local_dir, ui).await.context("updating metadata")?;
            read_product_bundle_metadata(&pbm_path)
                .await
                .with_context(|| format!("loading product metadata from {:?}", pbm_path))?
        }
    };
    tracing::debug!("fetch_data_for_product_bundle_v1, product_url {:?}", product_url);
    fetch_data_for_product_bundle_v1(&product_bundle_metadata, &product_url, local_dir, ui)
        .await
        .context("getting product data")?;

    let product_name = product_bundle_metadata.name;

    let repo_name = if let Some(repo_name) = &cmd.repository {
        repo_name.clone()
    } else if !product_name.is_empty() {
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
        ffx_bail!("invalid repository name {}: {}", repo_name, err);
    }

    // Register a repository with the daemon if we downloaded any packaging artifacts.
    let repo_path = local_dir.join(&product_name).join("packages");
    if repo_path.exists() {
        let repo_path =
            repo_path.canonicalize().with_context(|| format!("canonicalizing {:?}", repo_path))?;
        tracing::debug!("Register a repository with the daemon at {:?}", repo_path);

        let repo_spec =
            RepositorySpec::Pm { path: repo_path.try_into().context("repo_path.try_into")? };
        repos
            .add_repository(&repo_name, &mut repo_spec.into())
            .await
            .context("communicating with ffx daemon")?
            .map_err(RepositoryError::from)
            .with_context(|| format!("registering repository {}", repo_name))?;

        tracing::debug!("Created repository named '{}'", repo_name);
    } else {
        tracing::debug!(
            "The repository was not registered with the daemon because path {:?} does not exist.",
            repo_path
        );
    }

    tracing::debug!(
        "Total fx product-bundle get runtime {} seconds.",
        start.elapsed().as_secs_f32()
    );
    tracing::debug!("End");
    Ok(())
}

/// Remove prior output directory, if necessary.
async fn make_way_for_output(local_dir: &Path, force: bool) -> Result<()> {
    tracing::debug!("make_way_for_output {:?}, force {}", local_dir, force);
    if local_dir.exists() {
        tracing::debug!("local_dir.exists {:?}", local_dir);
        if std::fs::read_dir(&local_dir).expect("reading dir").next().is_none() {
            tracing::debug!("The dir is empty (which is okay) {:?}", local_dir);
        } else if force {
            if !(local_dir.join("info").exists() && local_dir.join("product_bundle.json").exists())
            {
                // Let's avoid `rm -rf /` or similar.
                ffx_bail!(
                    "The directory does not resemble an old product \
                    bundle. For caution's sake, please remove the output \
                    directory {:?} by hand and try again.",
                    local_dir
                );
            }
            std::fs::remove_dir_all(&local_dir)
                .with_context(|| format!("removing output dir {:?}", local_dir))?;
            assert!(!local_dir.exists());
            tracing::debug!("Removed all of {:?}", local_dir);
        } else {
            ffx_bail!(
                "The output directory already exists. Please provide \
                another directory to write to, or use --force to overwrite the \
                contents of {:?}.",
                local_dir
            );
        }
    }
    Ok(())
}

/// Read the product bundle metadata from the PBM file at 'pbm_path'.
///
/// pbm_path must point to a single pbm. If more than one is found, an error
/// will be returned.
async fn read_product_bundle_metadata(pbm_path: &Path) -> Result<sdk_metadata::ProductBundleV1> {
    use fms::{find_product_bundle, Entries};
    tracing::debug!("read_product_bundle_metadata {:?}", pbm_path);
    let entries =
        Entries::from_path_list(&[pbm_path.to_path_buf()]).await.context("loading fms entries")?;
    let pbm =
        find_product_bundle(&entries, /*fms_name=*/ &None).context("finding pbm in entries")?;
    Ok(pbm.to_owned())
}

#[cfg(test)]
mod test {
    use {super::*, tempfile};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_make_way_for_output() {
        let test_dir = tempfile::TempDir::new().expect("temp dir");

        make_way_for_output(&test_dir.path(), /*force=*/ false).await.expect("empty dir is okay");

        std::fs::create_dir(&test_dir.path().join("foo")).expect("make_dir foo");
        std::fs::File::create(test_dir.path().join("info")).expect("create info");
        std::fs::File::create(test_dir.path().join("product_bundle.json"))
            .expect("create product_bundle.json");
        make_way_for_output(&test_dir.path(), /*force=*/ true).await.expect("rm dir is okay");

        let test_dir = tempfile::TempDir::new().expect("temp dir");
        std::fs::create_dir(&test_dir.path().join("foo")).expect("make_dir foo");
        assert!(make_way_for_output(&test_dir.path(), /*force=*/ false).await.is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_read_product_bundle_metadata() {
        let test_dir = tempfile::TempDir::new().expect("temp dir");

        assert!(read_product_bundle_metadata(&test_dir.path()).await.is_err());

        let pbm_path = test_dir.path().join("product_bundle.json");
        std::fs::File::create(&pbm_path).expect("create pbm_path");
        assert!(read_product_bundle_metadata(&pbm_path).await.is_err());
    }
}
