// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Access utilities for SDK metadata.
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
        in_tree::pbms_from_tree,
        sdk::{
            fetch_fms_entries_from_sdk, fetch_from_gcs, local_entries, local_images_dir,
            local_packages_dir,
        },
    },
    anyhow::{bail, Context, Result},
    ffx_config::sdk::SdkVersion,
    fms::{find_product_bundle, Entries},
    std::{
        io::Write,
        path::{Path, PathBuf},
    },
};

mod in_tree;
mod sdk;

/// Load FMS Entries from the SDK or in-tree build.
///
/// `update_metadata`: Only used when downloading metadata. If working in-tree
///                    (`SdkVersion::InTree`) then update_metadata is ignored.
///    - If true, new metadata will be downloaded from gcs.
///    - If false, no network IO will be performed.
pub async fn get_pbms(update_metadata: bool) -> Result<Entries> {
    let sdk = ffx_config::get_sdk().await.context("PBMS ffx config get sdk")?;
    match sdk.get_version() {
        SdkVersion::Version(version) => {
            if update_metadata {
                fetch_fms_entries_from_sdk(version).await.context("fetch from sdk")?;
            }
            Ok(local_entries(version).await.context("read pbms entries")?)
        }
        SdkVersion::InTree => {
            Ok(pbms_from_tree(sdk.get_path_prefix()).await.context("read from in-tree")?)
        }
        SdkVersion::Unknown => bail!("Unable to determine SDK version vs. in-tree"),
    }
}

/// Download data related to the product.
///
/// The emulator may then be run with the data downloaded.
///
/// If `product_name` is None and only one viable PBM is available, that entry
/// is used.
///
/// `writer` is used to output user messages.
pub async fn get_product_data<W>(product_name: &Option<String>, writer: &mut W) -> Result<()>
where
    W: Write + Sync,
{
    let sdk = ffx_config::get_sdk().await.context("PBMS ffx config get sdk")?;
    match sdk.get_version() {
        SdkVersion::Version(version) => Ok(get_product_data_sdk(version, product_name, writer)
            .await
            .context("read pbms entries")?),
        SdkVersion::InTree => {
            write!(
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
    writer: &mut W,
) -> Result<()>
where
    W: Write + Sync,
{
    let entries = get_pbms(/*update_metadata=*/ true).await.context("get pbms")?;
    let product_bundle =
        find_product_bundle(&entries, product_name).context("find product bundle")?;
    writeln!(writer, "Get product data for {:?}", product_bundle.name)?;
    let local_dir = local_images_dir(version, &product_bundle.name).await?;
    for image in &product_bundle.images {
        writeln!(writer, "    image: {:?}", image)?;
        fetch_by_format(&image.format, &image.base_uri, &local_dir)
            .await
            .with_context(|| format!("Images for {}.", product_bundle.name))?;
    }
    let local_dir = local_packages_dir(version, &product_bundle.name).await?;
    for package in &product_bundle.packages {
        writeln!(writer, "    package: {:?}", package.repo_uri)?;
        fetch_by_format(&package.format, &package.repo_uri, &local_dir)
            .await
            .with_context(|| format!("Packages for {}.", product_bundle.name))?;
    }
    Ok(())
}

/// Download and expand data.
///
/// For a directory, all files in the directory are downloaded.
/// For a .tgz file, the file is downloaded and expanded.
async fn fetch_by_format(format: &str, uri: &str, local_dir: &Path) -> Result<()> {
    match format {
        "files" | "tgz" => fetch_bundle_uri(uri, &local_dir).await,
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
async fn fetch_bundle_uri(uri: &str, local_dir: &Path) -> Result<()> {
    if uri.starts_with("gs://") {
        fetch_from_gcs(uri, local_dir).await.context("Download from GCS.")?;
    } else if uri.starts_with("http://") || uri.starts_with("https://") {
        fetch_from_web(uri, local_dir).await?;
    } else if uri.starts_with("file://") {
        fetch_from_path(Path::new(uri.trim_start_matches("file://")), local_dir).await?;
    } else if !uri.contains(":") {
        // If there is no ":" in the string, assume it's a local file path.
        fetch_from_path(Path::new(uri), local_dir).await?;
    } else {
        bail!("Unexpected URI scheme in ({:?})", uri);
    }
    Ok(())
}

async fn fetch_from_web(_uri: &str, _local_dir: &Path) -> Result<()> {
    // TODO(fxbug.dev/93850): implement pbms.
    unimplemented!();
}

async fn fetch_from_path(_path: &Path, _local_dir: &Path) -> Result<()> {
    // TODO(fxbug.dev/93850): implement pbms.
    unimplemented!();
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        ffx_config::ConfigLevel,
        std::{fs::File, io::Write},
        tempfile::TempDir,
    };

    const CORE_JSON: &str = include_str!("../test_data/test_core.json");
    const IMAGES_JSON: &str = include_str!("../test_data/test_images.json");
    const PRODUCT_BUNDLE_JSON: &str = include_str!("../test_data/test_product_bundle.json");

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
        let entries = get_pbms(/*update_metadata=*/ false).await.expect("get pbms");
        assert!(!entries.entry("test_fake-x64").is_none());
    }
}
