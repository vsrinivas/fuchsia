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
        sdk::{fetch_from_sdk, local_entries},
    },
    anyhow::{bail, Context, Result},
    ffx_config::sdk::SdkVersion,
    fms::Entries,
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
                fetch_from_sdk(version).await.context("fetch from sdk")?;
            }
            Ok(local_entries().await.context("read pbms entries")?)
        }
        SdkVersion::InTree => {
            Ok(pbms_from_tree(sdk.get_path_prefix()).await.context("read from in-tree")?)
        }
        SdkVersion::Unknown => bail!("Unable to determine SDK version vs. in-tree"),
    }
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
