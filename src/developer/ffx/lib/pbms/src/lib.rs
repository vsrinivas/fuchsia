// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Access utilities for product metadata.
//!
//! This is a collection of helper functions wrapping the FMS and GCS libs.
//!
//! The metadata can be loaded from a variety of sources. The initial places are
//! GCS and the local build.
//!
//! Call `product_bundle_urls()` to get a set of URLs for each product bundle.
//!
//! Call `fms_entries_from()` to get FMS entries from a particular repo. The
//! entries include product bundle metadata, physical device specifications, and
//! virtual device specifications. Each FMS entry has a unique name to identify
//! that entry.
//!
//! These FMS entry names are suitable to present to the user. E.g. the name of
//! a product bundle is also the name of the product bundle metadata entry.

use {
    crate::{
        pbms::{
            fetch_product_metadata, get_product_data_from_gcs, local_path_helper,
            path_from_file_url, pb_dir_name, pb_names_from_path, pbm_repo_list,
            CONFIG_STORAGE_PATH, GS_SCHEME,
        },
        repo_info::RepoInfo,
    },
    ::gcs::client::{DirectoryProgress, FileProgress, ProgressResult},
    anyhow::{anyhow, bail, Context, Result},
    errors::ffx_bail,
    ffx_config::sdk,
    fms::Entries,
    futures::TryStreamExt as _,
    itertools::Itertools as _,
    sdk_metadata::ProductBundle,
    std::path::{Path, PathBuf},
};

pub use crate::pbms::{fetch_data_for_product_bundle_v1, get_product_dir, get_storage_dir};

mod gcs;
mod pbms;
mod repo_info;

/// Load a product bundle by name, uri, or local path.
/// This is capable of loading both v1 and v2 ProductBundles.
pub async fn load_product_bundle(product_bundle: &Option<String>) -> Result<ProductBundle> {
    tracing::debug!("Loading a product bundle: {:?}", product_bundle);

    //  If `product_bundle` is a local path, load it directly.
    if let Some(path) = product_bundle.as_ref().map(|s| Path::new(s)).filter(|p| p.exists()) {
        return ProductBundle::try_load_from(path);
    }

    // Otherwise, use the `fms` crate to fetch and parse the product bundle by name or uri.
    let product_url =
        select_product_bundle(product_bundle).await.context("Selecting product bundle")?;
    let name = product_url.fragment().expect("Product name is required.");

    let fms_entries = fms_entries_from(&product_url).await.context("get fms entries")?;
    let product = fms::find_product_bundle(&fms_entries, &Some(name.to_string()))
        .context("problem with product_bundle")?
        .to_owned();
    Ok(ProductBundle::V1(product))
}

/// For each non-local URL in ffx CONFIG_METADATA, fetch updated info.
pub async fn update_metadata_all<F>(output_dir: &Path, progress: &mut F) -> Result<()>
where
    F: FnMut(DirectoryProgress<'_>, FileProgress<'_>) -> ProgressResult,
{
    tracing::debug!("update_metadata_all");
    let repos = pbm_repo_list().await.context("getting repo list")?;
    async_fs::create_dir_all(&output_dir).await.context("create directory")?;
    for repo_url in repos {
        if repo_url.scheme() != GS_SCHEME {
            // There's no need to fetch local files or unrecognized schemes.
            continue;
        }
        fetch_product_metadata(&repo_url, &output_dir.join(pb_dir_name(&repo_url)), progress)
            .await
            .context("fetching product metadata")?;
    }
    Ok(())
}

/// Update metadata from given url.
pub async fn update_metadata_from<F>(
    product_url: &url::Url,
    output_dir: &Path,
    progress: &mut F,
) -> Result<()>
where
    F: FnMut(DirectoryProgress<'_>, FileProgress<'_>) -> ProgressResult,
{
    tracing::debug!("update_metadata_from");
    fetch_product_metadata(&product_url, output_dir, progress)
        .await
        .context("fetching product metadata")
}

/// Gather a list of PBM reference URLs which include the product bundle entry
/// name.
///
/// Tip: Call `update_metadata()` to update the info (or not, if the intent is
///      to peek at what's there without updating).
pub async fn product_bundle_urls() -> Result<Vec<url::Url>> {
    tracing::debug!("product_bundle_urls");
    let mut result = Vec::new();

    // Collect product bundle URLs from the file paths in ffx config.
    for repo in pbm_repo_list().await.context("getting repo list")? {
        if let Some(path) = &path_from_file_url(&repo) {
            let names =
                pb_names_from_path(&Path::new(&path)).context("loading product bundle names")?;
            for name in names {
                let mut product_url = repo.to_owned();
                product_url.set_fragment(Some(&name));
                result.push(product_url);
            }
        }
    }

    let storage_path: PathBuf =
        ffx_config::get(CONFIG_STORAGE_PATH).await.context("getting CONFIG_STORAGE_PATH")?;
    if !storage_path.is_dir() {
        // Early out before calling read_dir.
        return Ok(result);
    }

    // Collect product bundle URLs from the downloaded information. These
    // entries may not be currently referenced in the ffx config. This is where
    // product bundles from old versions will be pulled in, for example.
    let mut dir_entries = async_fs::read_dir(storage_path).await.context("reading vendors dir")?;
    while let Some(dir_entry) = dir_entries.try_next().await.context("reading directory")? {
        if dir_entry.path().is_dir() {
            if let Ok(repo_info) = RepoInfo::load(&dir_entry.path().join("info")) {
                let names = pb_names_from_path(&dir_entry.path().join("product_bundles.json"))?;
                for name in names {
                    let repo = format!("{}#{}", repo_info.metadata_url, name);
                    result.push(
                        url::Url::parse(&repo)
                            .with_context(|| format!("parsing metadata URL {:?}", repo))?,
                    );
                }
            }
        }
    }
    Ok(result)
}

/// Gather all the fms entries from a given product_url.
///
/// If `product_url` is None or not a URL, then an attempt will be made to find
/// default entries.
pub async fn fms_entries_from(product_url: &url::Url) -> Result<Entries> {
    tracing::debug!("fms_entries_from");
    let path = get_metadata_glob(product_url).await.context("getting metadata")?;
    let mut entries = Entries::new();
    entries.add_from_path(&path).context("adding entries")?;
    Ok(entries)
}

/// The default behavior for when there are more than one matching
/// product-bundle.
///
/// This function should only be called with an iterator of `url::Url`s that has
/// 2 or more entries, and has already been sorted & reversed.
fn default_pbm_of_many<I>(
    mut urls: I,
    sdk_version: &sdk::SdkVersion,
    looking_for: Option<String>,
) -> Result<url::Url>
where
    I: Iterator<Item = url::Url> + Clone,
{
    let extra_message = if let Some(looking_for) = looking_for {
        format!(" for `{}`", looking_for)
    } else {
        String::new()
    };
    let formatted =
        urls.clone().map(|url| format!("`{}`", url)).collect::<Vec<String>>().join("\n");
    println!(
        "Multiple product bundles found{extra_message}. To choose a specific product, pass \
        in a full URL from the following:\n{formatted}",
        extra_message = extra_message,
        formatted = formatted
    );
    tracing::info!("Product bundles: {}", formatted);
    let first = match sdk_version {
        sdk::SdkVersion::Version(version) => {
            tracing::info!("Sdk version: `{}`", version);
            let mut matching_sdk_version = urls.filter(|url| url.to_string().contains(version));
            let first = matching_sdk_version.next();
            match first {
                Some(first) => first,
                None => {
                    ffx_bail!("There were no product-bundles for your sdk version: `{}`", version)
                }
            }
        }
        sdk::SdkVersion::InTree | sdk::SdkVersion::Unknown => match urls.next() {
            Some(first) => first,
            None => {
                return Err(anyhow!(
                    "This function should only be called with an iterator with at least 2 entries."
                ))
            }
        },
    };
    println!(
        "Defaulting to the first valid product-bundle in sorted order: `{first}`",
        first = first.to_string()
    );
    Ok(first)
}

/// Find a product bundle url and name for `product_url`.
///
/// If product_url is
/// - None and there is only one product bundle available, use it.
/// - Some(product name) and a url with that fragment is found, use it.
/// - Some(full product url with fragment) use it.
/// If a match is not found or multiple matches are found, fail with an error
/// message.
///
/// Tip: Call `update_metadata()` to get up to date choices (or not, if the
///      intent is to select from what's already there).
pub async fn select_product_bundle(looking_for: &Option<String>) -> Result<url::Url> {
    tracing::debug!("select_product_bundle");
    let mut urls = product_bundle_urls().await.context("getting product bundle URLs")?;
    // Sort the URLs lexigraphically, then reverse them so the most recent
    // version strings will be first.
    let sdk = ffx_config::get_sdk().await?;
    let sdk_version = sdk.get_version();
    urls.sort();
    urls.reverse();
    if let Some(looking_for) = &looking_for {
        let matches = urls.into_iter().filter(|url| {
            return url.as_str() == looking_for
                || url.fragment().expect("product_urls must have fragment") == looking_for;
        });
        match matches.at_most_one() {
            Ok(Some(m)) => Ok(m),
            Ok(None) => bail!(
                "{}",
                "A product bundle with that name was not found, please check the spelling and try again."
            ),
            Err(matches) => default_pbm_of_many(matches, sdk_version, Some(looking_for.to_string())),
        }
    } else {
        match urls.into_iter().at_most_one() {
            Ok(Some(url)) => Ok(url),
            Ok(None) => bail!("There are no product bundles available."),
            Err(urls) => default_pbm_of_many(urls, sdk_version, None),
        }
    }
}

/// Determine whether the data for `product_url` is downloaded and ready to be
/// used.
pub async fn is_pb_ready(product_url: &url::Url) -> Result<bool> {
    assert!(product_url.as_str().contains("#"));
    Ok(get_images_dir(product_url).await.context("getting images dir")?.is_dir())
}

/// Download data related to the product.
///
/// The emulator may then be run with the data downloaded.
///
/// If `product_bundle_url` is None and only one viable PBM is available, that entry
/// is used.
///
/// `writer` is used to output user messages.
pub async fn get_product_data<F>(
    product_url: &url::Url,
    output_dir: &std::path::Path,
    progress: &mut F,
) -> Result<()>
where
    F: FnMut(DirectoryProgress<'_>, FileProgress<'_>) -> ProgressResult,
{
    tracing::debug!("get_product_data {:?} to {:?}", product_url, output_dir);
    if product_url.scheme() == "file" {
        tracing::info!("There's no data download necessary for local products.");
        return Ok(());
    }
    if product_url.scheme() != GS_SCHEME {
        tracing::info!("Only GCS downloads are supported at this time.");
        return Ok(());
    }
    get_product_data_from_gcs(product_url, output_dir, progress)
        .await
        .context("reading pbms entries")?;
    Ok(())
}

/// Determine the path to the product images data.
pub async fn get_images_dir(product_url: &url::Url) -> Result<PathBuf> {
    assert!(!product_url.as_str().is_empty());
    let name = product_url.fragment().expect("a URI fragment is required");
    assert!(!name.is_empty());
    assert!(!name.contains("/"));
    local_path_helper(product_url, &format!("{}/images", name), /*dir=*/ true).await
}

/// Determine the path to the product packages data.
pub async fn get_packages_dir(product_url: &url::Url) -> Result<PathBuf> {
    assert!(!product_url.as_str().is_empty());
    let name = product_url.fragment().expect("a URI fragment is required");
    assert!(!name.is_empty());
    assert!(!name.contains("/"));
    local_path_helper(product_url, &format!("{}/packages", name), /*dir=*/ true).await
}

/// Determine the path to the local product metadata directory.
pub async fn get_metadata_dir(product_url: &url::Url) -> Result<PathBuf> {
    assert!(!product_url.as_str().is_empty());
    assert!(!product_url.fragment().is_none());
    Ok(get_metadata_glob(product_url)
        .await
        .context("getting metadata")?
        .parent()
        .expect("Metadata files should have a parent")
        .to_path_buf())
}

/// Determine the glob path to the product metadata.
///
/// A glob path may have wildcards, such as "file://foo/*.json".
pub async fn get_metadata_glob(product_url: &url::Url) -> Result<PathBuf> {
    assert!(!product_url.as_str().is_empty());
    assert!(!product_url.fragment().is_none());
    local_path_helper(product_url, "product_bundles.json", /*dir=*/ false).await
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pbms::CONFIG_METADATA;
    use ::gcs::client::ProgressResponse;
    use ffx_config::ConfigLevel;
    use serde_json;
    use std::{fs::File, io::Write};
    use tempfile::TempDir;

    const CORE_JSON: &str = include_str!("../test_data/test_core.json");
    const IMAGES_JSON: &str = include_str!("../test_data/test_images.json");
    const PRODUCT_BUNDLE_JSON: &str = include_str!("../test_data/test_product_bundle.json");

    // Disabling this test until a test config can be modified without altering
    // the local user's config.
    #[ignore]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_pbms() {
        let _env = ffx_config::test_init().await.expect("create test config");
        let temp_dir = TempDir::new().expect("temp dir");
        let temp_path = temp_dir.path();

        let manifest_path = temp_dir.path().join("sdk/manifest");
        std::fs::create_dir_all(&manifest_path).expect("create dir");
        let mut core_file = File::create(manifest_path.join("core")).expect("create core");
        core_file.write_all(CORE_JSON.as_bytes()).expect("write core file");
        drop(core_file);

        let mut images_file =
            File::create(temp_path.join("images.json")).expect("create images file");
        images_file.write_all(IMAGES_JSON.as_bytes()).expect("write images file");
        drop(images_file);

        let mut pbm_file =
            File::create(temp_path.join("product_bundle.json")).expect("create images file");
        pbm_file.write_all(PRODUCT_BUNDLE_JSON.as_bytes()).expect("write pbm file");
        drop(pbm_file);

        let sdk_root = temp_path.to_str().expect("path to str");
        ffx_config::query("sdk.root")
            .level(Some(ConfigLevel::User))
            .set(sdk_root.into())
            .await
            .expect("set sdk root path");
        ffx_config::query("sdk.type")
            .level(Some(ConfigLevel::User))
            .set("in-tree".into())
            .await
            .expect("set sdk type");
        ffx_config::query(CONFIG_METADATA)
            .level(Some(ConfigLevel::User))
            .set(serde_json::json!([""]))
            .await
            .expect("set pbms metadata");
        let output_dir = temp_dir.path().join("output_dir");
        update_metadata_all(&output_dir, &mut |_d, _f| Ok(ProgressResponse::Continue))
            .await
            .expect("get pbms");
        let urls = product_bundle_urls().await.expect("get pbms");
        assert!(!urls.is_empty());
    }
}
