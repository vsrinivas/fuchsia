// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        file::FileResolver,
        manifest::{
            v1::{
                FlashManifest as FlashManifestV1, Partition as PartitionV1, Product as ProductV1,
            },
            v2::FlashManifest as FlashManifestV2,
            Flash,
        },
    },
    anyhow::Result,
    async_trait::async_trait,
    ffx_flash_args::{FlashCommand, OemFile},
    fidl_fuchsia_developer_bridge::FastbootProxy,
    serde::{Deserialize, Serialize},
    std::convert::From,
    std::io::Write,
};

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub(crate) struct FlashManifest {
    pub(crate) hw_revision: String,
    pub(crate) products: Vec<Product>,
}

#[derive(Clone, Default, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub(crate) struct Product {
    pub(crate) name: String,
    #[serde(default)]
    pub(crate) bootloader_partitions: Vec<Partition>,
    pub(crate) partitions: Vec<Partition>,
    #[serde(default)]
    pub(crate) oem_files: Vec<ExplicitOemFile>,
    #[serde(default)]
    pub(crate) requires_unlock: bool,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub(crate) struct Partition {
    pub(crate) name: String,
    pub(crate) path: String,
    #[serde(default)]
    pub(crate) condition: Option<Condition>,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub(crate) struct Condition {
    pub(crate) variable: String,
    pub(crate) value: String,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub(crate) struct ExplicitOemFile {
    pub(crate) command: String,
    pub(crate) path: String,
}

impl From<&ExplicitOemFile> for OemFile {
    fn from(f: &ExplicitOemFile) -> OemFile {
        OemFile::new(f.command.clone(), f.path.clone())
    }
}

impl From<&Partition> for PartitionV1 {
    fn from(p: &Partition) -> PartitionV1 {
        PartitionV1::new(
            p.name.clone(),
            p.path.clone(),
            p.condition.as_ref().map(|c| c.variable.clone()),
            p.condition.as_ref().map(|c| c.value.clone()),
        )
    }
}

impl From<&Product> for ProductV1 {
    fn from(p: &Product) -> ProductV1 {
        ProductV1 {
            name: p.name.clone(),
            bootloader_partitions: p.bootloader_partitions.iter().map(|p| p.into()).collect(),
            partitions: p.partitions.iter().map(|p| p.into()).collect(),
            oem_files: p.oem_files.iter().map(|f| f.into()).collect(),
            requires_unlock: p.requires_unlock,
        }
    }
}

impl From<&FlashManifest> for FlashManifestV2 {
    fn from(p: &FlashManifest) -> FlashManifestV2 {
        FlashManifestV2 {
            hw_revision: p.hw_revision.clone(),
            v1: FlashManifestV1(p.products.iter().map(|p| p.into()).collect()),
        }
    }
}

#[async_trait(?Send)]
impl Flash for FlashManifest {
    async fn flash<W, F>(
        &self,
        writer: &mut W,
        file_resolver: &mut F,
        fastboot_proxy: FastbootProxy,
        cmd: FlashCommand,
    ) -> Result<()>
    where
        W: Write,
        F: FileResolver + Sync,
    {
        let v2: FlashManifestV2 = self.into();
        v2.flash(writer, file_resolver, fastboot_proxy, cmd).await
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;
    use crate::test::{setup, TestResolver};
    use serde_json::from_str;
    use std::path::PathBuf;
    use tempfile::NamedTempFile;

    const MINIMUM_MANIFEST: &'static str = r#"{
        "hw_revision": "rev_test",
        "products": [
            {
                "name": "zedboot",
                "partitions": [
                    {"name": "test1", "path": "path1"},
                    {"name": "test2", "path": "path2"},
                    {"name": "test3", "path": "path3"},
                    {
                        "name": "test4",
                        "path": "path4",
                        "condition": {
                            "variable": "var",
                            "value": "val"
                        }
                    }
                ]
            }
        ]
    }"#;

    const FULL_MANIFEST: &'static str = r#"{
        "hw_revision": "rev_test",
        "products": [
            {
                "name": "zedboot",
                "bootloader_partitions": [
                    {"name": "test1", "path": "path1"},
                    {"name": "test2", "path": "path2"},
                    {"name": "test3", "path": "path3"},
                    {
                        "name": "test4",
                        "path": "path4",
                        "condition": {
                            "variable": "var",
                            "value": "val"
                        }
                    }
                ],
                "partitions": [
                    {"name": "test1", "path": "path1"},
                    {"name": "test2", "path": "path2"},
                    {"name": "test3", "path": "path3"},
                    {"name": "test4", "path": "path4"}
                ],
                "oem_files": [
                    {"command": "test1", "path": "path1"},
                    {"command": "test2", "path": "path2"},
                    {"command": "test3", "path": "path3"},
                    {"command": "test4", "path": "path4"}
                ]
            }
        ]
    }"#;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_minimal_manifest_succeeds() -> Result<()> {
        let v: FlashManifest = from_str(MINIMUM_MANIFEST)?;
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        let (state, proxy) = setup();
        state.lock().unwrap().variables.push("rev_test-b4".to_string());
        let mut writer = Vec::<u8>::new();
        v.flash(
            &mut writer,
            &mut TestResolver::new(),
            proxy,
            FlashCommand {
                manifest: Some(PathBuf::from(tmp_file_name)),
                product: "zedboot".to_string(),
                ..Default::default()
            },
        )
        .await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_full_manifest_succeeds() -> Result<()> {
        let v: FlashManifest = from_str(FULL_MANIFEST)?;
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        let (state, proxy) = setup();
        state.lock().unwrap().variables.push("rev_test-b4".to_string());
        let mut writer = Vec::<u8>::new();
        v.flash(
            &mut writer,
            &mut TestResolver::new(),
            proxy,
            FlashCommand {
                manifest: Some(PathBuf::from(tmp_file_name)),
                product: "zedboot".to_string(),
                ..Default::default()
            },
        )
        .await
    }
}
