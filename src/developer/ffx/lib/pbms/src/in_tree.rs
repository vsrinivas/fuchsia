// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Access utilities for metadata built in-tree (that is, in a local Fuchsia
//! build rather than the SDK).

use {
    anyhow::{anyhow, Context, Result},
    errors::{ffx_bail, ffx_error},
    fms::Entries,
    serde::Deserialize,
    std::{
        fs::File,
        io::BufReader,
        path::{Path, PathBuf},
    },
};

/// Outer type in a "images.json" file.
#[derive(Default, Deserialize)]
struct Images(Vec<Image>);

/// Individual records in an "images.json" file.
#[derive(Default, Deserialize)]
struct Image {
    pub name: String,
    pub path: String,
    // Ignore the rest of the fields.
}

/// Load FMS Entries from the local in-tree build (rather than from the SDK).
///
/// Tip: `let build_path = sdk.get_path_prefix();`.
pub(crate) async fn pbms_from_tree(build_path: &Path) -> Result<Entries> {
    if let Ok(entries) = from_config().await {
        return Ok(entries);
    }

    // The product_bundle.json path will be a relative path under `build_path`.
    // The "images.json" file provides an index of named items to paths. Open
    // "images.json" and search for the path of the "product_bundle" entry.
    let manifest_path = build_path.join("images.json");
    let images: Images = File::open(manifest_path.clone())
        .map_err(|e| {
            ffx_error!(
                "Cannot open file {:?}\nerror: {:?}. Doing `fx build` may help.",
                manifest_path,
                e
            )
        })
        .map(BufReader::new)
        .map(serde_json::from_reader)?
        .map_err(|e| anyhow!("json parsing error {}", e))?;
    let product_bundle =
        images.0.iter().find(|i| i.name == "product_bundle").map(|i| i.path.clone());
    let product_bundle_path = if let Some(pb) = product_bundle {
        build_path.join(pb)
    } else {
        ffx_bail!(
            "Could not find the Product Bundle in the SDK. Build a product (`fx build`) and retry."
        );
    };
    let mut entries = Entries::new();
    let file = File::open(&product_bundle_path).context("open product bundle")?;
    entries.add_json(&mut BufReader::new(file)).context("add metadata")?;
    Ok(entries)
}

/// Initialize the FMS database from paths in config.
///
/// Look for appropriate .json files in the "pbms.in-tree.files" config entry
/// and import metadata from them.
pub async fn from_config() -> Result<Entries> {
    let files: Vec<PathBuf> =
        ffx_config::get("pbms.in-tree.files").await.context("ffx_config::get fms data")?;
    const SDK_ROOT: &str = "{sdk.root}/";
    let sdk_root = ffx_config::get_sdk().await.context("get sdk")?.get_path_prefix().to_path_buf();
    let files = files
        .iter()
        .map(|path| {
            // If the path starts with SDK_ROOT, replace it with sdk_root.
            match path.strip_prefix(SDK_ROOT) {
                Ok(s) => sdk_root.join(&s),
                Err(_) => path.to_path_buf(),
            }
        })
        .collect::<Vec<_>>();
    Ok(Entries::from_path_list(&files).await.context("Loading config pbms.in-tree.files")?)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        std::{fs::File, io::Write},
        tempfile::TempDir,
    };

    const IMAGES_JSON: &str = include_str!("../test_data/test_images.json");
    const PRODUCT_BUNDLE_JSON: &str = include_str!("../test_data/test_product_bundle.json");

    #[fuchsia_async::run_singlethreaded(test)]
    #[should_panic(expected = "Cannot open file")]
    async fn test_pbms_from_tree_missing_file() {
        let temp_dir = TempDir::new().expect("temp dir");
        let build_path = temp_dir.path();
        let _entries = pbms_from_tree(build_path).await.expect("read from tree");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_pbms_from_tree() {
        let temp_dir = TempDir::new().expect("temp dir");
        let build_path = temp_dir.path();

        let mut images_file =
            File::create(build_path.join("images.json")).expect("create images file");
        images_file.write_all(IMAGES_JSON.as_bytes()).expect("write images file");
        drop(images_file);

        let mut pbm_file =
            File::create(build_path.join("product_bundle.json")).expect("create images file");
        pbm_file.write_all(PRODUCT_BUNDLE_JSON.as_bytes()).expect("write pbm file");
        drop(pbm_file);

        let entries = pbms_from_tree(&build_path).await.expect("read from tree");
        assert!(!entries.entry("test_fake-x64").is_none());
    }
}
