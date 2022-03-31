// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Access utilities for product metadata.
//!
//! This is a collection of helper functions wrapping the FMS and GCS libs.
//!
//! The metadata can be loaded from a variety of sources. The initial places are
//! GCS and the local build. One or the other is used based on the ffx_config
//! sdk settings.
//!
//! Call `get_pbms()` to get a set of FMS entries. The entries include product
//! bundle metadata, physical device specifications, and virtual device
//! specifications. Each FMS entry has a unique name to identify that entry.
//!
//! These FMS entry names are suitable to present to the user. E.g. the name of
//! a product bundle is also the name of the product bundle metadata entry.

use {
    crate::{
        gcs::fetch_from_gcs,
        in_tree::pbms_from_tree,
        sdk::{local_images_dir, local_metadata_dir, local_packages_dir},
    },
    anyhow::{bail, Context, Result},
    ffx_config::sdk::SdkVersion,
    fms::{find_product_bundle, Entries},
    std::{
        io::Write,
        path::{Path, PathBuf},
    },
};

mod gcs;
mod in_tree;
mod sdk;

/// Load FMS Entries from the SDK or in-tree build.
///
/// `update_metadata`: Only used when downloading metadata. If working in-tree
///                    (`SdkVersion::InTree`) then update_metadata is ignored.
///    - If true, new metadata will be downloaded from gcs.
///    - If false, no network IO will be performed.
pub async fn get_pbms<W>(update_metadata: bool, verbose: bool, writer: &mut W) -> Result<Entries>
where
    W: Write + Sync,
{
    let sdk = ffx_config::get_sdk().await.context("PBMS ffx config get sdk")?;
    let version = match sdk.get_version() {
        SdkVersion::Version(version) => version,
        SdkVersion::InTree => "",
        SdkVersion::Unknown => bail!("Unable to determine SDK version vs. in-tree"),
    };
    let sdk_root = sdk.get_path_prefix();
    let repos: Vec<String> = ffx_config::get::<Vec<String>, _>("pbms.metadata").await?;
    let repos: Vec<String> = repos
        .iter()
        .map(|s| expand_placeholders(s, &version, &sdk_root.to_string_lossy()))
        .collect();
    if repos.len() == 0 || repos == vec![""] {
        // If no URLs are listed, assume that ffx is running within the tree and
        // find the local build.
        Ok(pbms_from_tree(&sdk_root).await?)
    } else {
        if update_metadata {
            fetch_product_metadata(&repos, version, verbose, writer)
                .await
                .context("fetch product metadata")?;
        }
        Ok(known_pbms(&repos, version).await.context("read pbms entries")?)
    }
}

/// Determine whether the data for `product_name` is downloaded and ready to be
/// used.
pub async fn is_pb_ready(product_name: &str) -> Result<bool> {
    Ok(get_data_dir(product_name).await?.is_dir())
}

/// Load FMS Entries from the local metadata.
///
/// Whether the sdk or local build is used is determined by ffx_config::sdk.
/// No attempt is made to refresh the metadata (no network IO).
pub(crate) async fn known_pbms(repos: &Vec<String>, version: &str) -> Result<Entries> {
    // The directory at `pb_path` might not exist and that's okay.
    let pb_path = local_metadata_dir(version).await.context("local metadata path")?;
    let mut entries = Entries::new();
    for uri in repos {
        if uri.starts_with("gs://") {
            let path = pb_file_name(uri);
            let file_path = pb_path.join(&path);
            if file_path.is_file() {
                entries.add_from_path(&file_path)?;
            }
        } else if uri.starts_with("http://") || uri.starts_with("https://") {
            // TODO(fxbug.dev/92773): add https support.
            unimplemented!();
        } else if uri.starts_with("file://") {
            const PREFIX_LEN: usize = "file://".len();
            entries.add_from_path(&Path::new(&uri[PREFIX_LEN..]))?;
        } else if !uri.contains(":") {
            // If there is no ":" in the string, assume it's a local file path.
            entries.add_from_path(&Path::new(uri))?;
        } else {
            bail!("Unexpected URI scheme in ({:?})", uri);
        }
    }
    Ok(entries)
}

/// Load FMS Entries for a given SDK `version`.
///
/// Expandable tags (e.g. "{foo}") in `repos` must already be expanded, do not
/// pass in repo URIs with expandable tags.
async fn fetch_product_metadata<W>(
    repos: &Vec<String>,
    version: &str,
    verbose: bool,
    writer: &mut W,
) -> Result<()>
where
    W: Write + Sync,
{
    writeln!(writer, "Getting product metadata.")?;
    let local_dir = local_metadata_dir(version).await.context("local metadata path")?;
    std::fs::create_dir_all(&local_dir)?;
    for repo_url in repos {
        let temp_dir = tempfile::tempdir_in(&local_dir).context("create temp dir")?;
        fetch_bundle_uri(&repo_url, &temp_dir.path(), verbose, writer).await?;
        let the_file = temp_dir.path().join("product_bundles.json");
        if the_file.is_file() {
            let hash = pb_file_name(&repo_url);
            async_fs::rename(&the_file, &local_dir.join(hash)).await.context("move temp file")?;
        }
    }
    Ok(())
}

/// Replace the {foo} placeholders in repo paths.
///
/// {version} is replaced with the Fuchsia SDK version string.
/// {sdk.root} is replaced with the SDK directory path.
fn expand_placeholders(uri: &str, version: &str, sdk_root: &str) -> String {
    uri.replace("{version}", version).replace("{sdk.root}", sdk_root)
}

/// Generate a (likely) unique name for the URL.
///
/// Multiple repos may have identical names for their product bundles.
/// Instead of using the given file name a hash of the URL is used.
fn pb_file_name(gcs_url: &str) -> String {
    use std::collections::hash_map::DefaultHasher;
    use std::hash::Hash;
    use std::hash::Hasher;
    let mut s = DefaultHasher::new();
    gcs_url.hash(&mut s);
    format!("{}{}", s.finish(), ".json")
}

/// Download data related to the product.
///
/// The emulator may then be run with the data downloaded.
///
/// If `product_name` is None and only one viable PBM is available, that entry
/// is used.
///
/// `writer` is used to output user messages.
pub async fn get_product_data<W>(
    product_name: &Option<String>,
    verbose: bool,
    writer: &mut W,
) -> Result<()>
where
    W: Write + Sync,
{
    let sdk = ffx_config::get_sdk().await.context("PBMS ffx config get sdk")?;
    match sdk.get_version() {
        SdkVersion::Version(version) => {
            Ok(get_product_data_sdk(version, product_name, verbose, writer)
                .await
                .context("read pbms entries")?)
        }
        SdkVersion::InTree => {
            writeln!(
                writer,
                "\
                There's no data download necessary for in-tree products.\
                \nSimply build the desired product (e.g. using `fx set ...` and `fx build`)."
            )?;
            Ok(())
        }
        SdkVersion::Unknown => bail!("Unable to determine SDK version vs. in-tree"),
    }
}

/// Determine the path to the product data.
pub async fn get_data_dir(product_name: &str) -> Result<PathBuf> {
    let sdk = ffx_config::get_sdk().await?;
    match sdk.get_version() {
        SdkVersion::Version(version) => local_images_dir(version, product_name).await,
        SdkVersion::InTree => Ok(sdk.get_path_prefix().to_path_buf()),
        SdkVersion::Unknown => bail!("Unable to determine SDK version vs. in-tree"),
    }
}

/// Determine the path to the local product metadata.
pub async fn get_metadata_dir(product_name: &str) -> Result<PathBuf> {
    let sdk = ffx_config::get_sdk().await.context("PBMS ffx config get sdk")?;
    match sdk.get_version() {
        SdkVersion::Version(version) => Ok(local_images_dir(version, product_name).await?),
        SdkVersion::InTree => Ok(sdk.get_path_prefix().to_path_buf()),
        SdkVersion::Unknown => bail!("Unable to determine SDK version vs. in-tree"),
    }
}

/// Helper for `get_product_data()`, see docs there.
///
/// 'version' is the sdk version. It is used in the path to differentiate
/// versions of the same product_name.
async fn get_product_data_sdk<W>(
    version: &str,
    product_name: &Option<String>,
    verbose: bool,
    writer: &mut W,
) -> Result<()>
where
    W: Write + Sync,
{
    let entries = get_pbms(/*update_metadata=*/ true, verbose, writer).await.context("get pbms")?;
    let product_bundle =
        find_product_bundle(&entries, product_name).context("find product bundle")?;
    writeln!(writer, "Getting product data for {:?}", product_bundle.name)?;
    let local_dir = local_images_dir(version, &product_bundle.name).await?;
    async_fs::create_dir_all(&local_dir).await?;
    for image in &product_bundle.images {
        if verbose {
            writeln!(writer, "    image: {:?}", image)?;
        } else {
            write!(writer, ".")?;
        }
        fetch_by_format(&image.format, &image.base_uri, &local_dir, verbose, writer)
            .await
            .with_context(|| format!("Images for {}.", product_bundle.name))?;
    }
    writeln!(writer, "Getting package data for {:?}", product_bundle.name)?;
    let local_dir = local_packages_dir(version, &product_bundle.name).await?;
    async_fs::create_dir_all(&local_dir).await?;
    for package in &product_bundle.packages {
        if verbose {
            writeln!(writer, "    package: {:?}", package.repo_uri)?;
        } else {
            write!(writer, ".")?;
            writer.flush()?;
        }
        fetch_by_format(&package.format, &package.repo_uri, &local_dir, verbose, writer)
            .await
            .with_context(|| format!("Packages for {}.", product_bundle.name))?;
    }
    writeln!(writer, "Download of product data for {:?} is complete.", product_bundle.name)?;
    if verbose {
        if let Some(parent) = local_dir.parent() {
            writeln!(writer, "Data written to \"{}\".", parent.display())?;
        }
    }
    Ok(())
}

/// Download and expand data.
///
/// For a directory, all files in the directory are downloaded.
/// For a .tgz file, the file is downloaded and expanded.
async fn fetch_by_format<W>(
    format: &str,
    uri: &str,
    local_dir: &Path,
    verbose: bool,
    writer: &mut W,
) -> Result<()>
where
    W: Write + Sync,
{
    match format {
        "files" | "tgz" => fetch_bundle_uri(uri, &local_dir, verbose, writer).await,
        _ =>
        // The schema currently defines only "files" or "tgz" (see RFC-100).
        // This error could be a typo in the product bundle or a new image
        // format has been added and this code needs an update.
        {
            bail!(
                "Unexpected image format ({:?}) in product bundle. \
            Supported formats are \"files\" and \"tgz\". \
            Please report as a bug.",
                format,
            )
        }
    }
}

/// Download data from any of the supported schemes listed in RFC-100, Product
/// Bundle, "bundle_uri".
///
/// Currently: "pattern": "^(?:http|https|gs|file):\/\/"
async fn fetch_bundle_uri<W>(
    uri: &str,
    local_dir: &Path,
    verbose: bool,
    writer: &mut W,
) -> Result<()>
where
    W: Write + Sync,
{
    if uri.starts_with("gs://") {
        fetch_from_gcs(uri, local_dir, verbose, writer).await.context("Download from GCS.")?;
    } else if uri.starts_with("http://") || uri.starts_with("https://") {
        fetch_from_web(uri, local_dir).await?;
    } else if uri.starts_with("file://") || !uri.contains(":") {
        // Since the file is already local, no fetch is necessary.
    } else {
        bail!("Unexpected URI scheme in ({:?})", uri);
    }
    Ok(())
}

async fn fetch_from_web(_uri: &str, _local_dir: &Path) -> Result<()> {
    // TODO(fxbug.dev/93850): implement pbms.
    unimplemented!();
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        ffx_config::ConfigLevel,
        serde_json,
        std::{fs::File, io::Write},
        tempfile::TempDir,
    };

    const CORE_JSON: &str = include_str!("../test_data/test_core.json");
    const IMAGES_JSON: &str = include_str!("../test_data/test_images.json");
    const PRODUCT_BUNDLE_JSON: &str = include_str!("../test_data/test_product_bundle.json");

    // Disabling this test until a test config can be modified without altering
    // the local user's config.
    #[ignore]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_pbms() {
        ffx_config::init(&[], None, None).expect("create test config");
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
        ffx_config::set(("sdk.root", ConfigLevel::User), sdk_root.into())
            .await
            .expect("set sdk root path");
        ffx_config::set(("sdk.type", ConfigLevel::User), "in-tree".into())
            .await
            .expect("set sdk type");
        ffx_config::set(("pbms.metadata", ConfigLevel::User), serde_json::json!([""]))
            .await
            .expect("set pbms metadata");
        let mut writer = Box::new(std::io::stdout());
        let entries =
            get_pbms(/*update_metadata=*/ false, /*verbose=*/ false, &mut writer)
                .await
                .expect("get pbms");
        assert!(!entries.entry("test_fake-x64").is_none());
    }
}
