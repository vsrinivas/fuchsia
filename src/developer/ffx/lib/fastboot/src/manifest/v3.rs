// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        common::{
            cmd::{ManifestParams, OemFile},
            file::FileResolver,
        },
        manifest::{
            v1::{
                FlashManifest as FlashManifestV1, Partition as PartitionV1, Product as ProductV1,
            },
            v2::FlashManifest as FlashManifestV2,
            Boot, Flash, Unlock,
        },
    },
    anyhow::Result,
    async_trait::async_trait,
    fidl_fuchsia_developer_ffx::FastbootProxy,
    serde::{Deserialize, Serialize},
    std::convert::From,
    std::io::Write,
};

#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
pub struct FlashManifest {
    pub hw_revision: String,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub credentials: Vec<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub products: Vec<Product>,
}

#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
pub struct Product {
    pub name: String,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub bootloader_partitions: Vec<Partition>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub partitions: Vec<Partition>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub oem_files: Vec<ExplicitOemFile>,
    #[serde(default)]
    pub requires_unlock: bool,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct Partition {
    pub name: String,
    pub path: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub condition: Option<Condition>,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct Condition {
    pub variable: String,
    pub value: String,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct ExplicitOemFile {
    pub command: String,
    pub path: String,
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
            credentials: p.credentials.iter().map(|c| c.clone()).collect(),
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
        cmd: ManifestParams,
    ) -> Result<()>
    where
        W: Write,
        F: FileResolver + Sync,
    {
        let v2: FlashManifestV2 = self.into();
        v2.flash(writer, file_resolver, fastboot_proxy, cmd).await
    }
}

#[async_trait(?Send)]
impl Unlock for FlashManifest {
    async fn unlock<W, F>(
        &self,
        writer: &mut W,
        file_resolver: &mut F,
        fastboot_proxy: FastbootProxy,
    ) -> Result<()>
    where
        W: Write,
        F: FileResolver + Sync,
    {
        let v2: FlashManifestV2 = self.into();
        v2.unlock(writer, file_resolver, fastboot_proxy).await
    }
}

#[async_trait(?Send)]
impl Boot for FlashManifest {
    async fn boot<W, F>(
        &self,
        writer: &mut W,
        file_resolver: &mut F,
        slot: String,
        fastboot_proxy: FastbootProxy,
        cmd: ManifestParams,
    ) -> Result<()>
    where
        W: Write,
        F: FileResolver + Sync,
    {
        let v2: FlashManifestV2 = self.into();
        v2.boot(writer, file_resolver, slot, fastboot_proxy, cmd).await
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;
    use crate::common::{IS_USERSPACE_VAR, REVISION_VAR};
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
        {
            let mut state = state.lock().unwrap();
            state.set_var(REVISION_VAR.to_string(), "rev_test-b4".to_string());
            state.set_var(IS_USERSPACE_VAR.to_string(), "no".to_string());
            state.set_var("var".to_string(), "val".to_string());
        }
        let mut writer = Vec::<u8>::new();
        v.flash(
            &mut writer,
            &mut TestResolver::new(),
            proxy,
            ManifestParams {
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
        {
            let mut state = state.lock().unwrap();
            state.set_var("var".to_string(), "val".to_string());
            state.set_var(IS_USERSPACE_VAR.to_string(), "no".to_string());
            state.set_var(REVISION_VAR.to_string(), "rev_test-b4".to_string());
        }
        let mut writer = Vec::<u8>::new();
        v.flash(
            &mut writer,
            &mut TestResolver::new(),
            proxy,
            ManifestParams {
                manifest: Some(PathBuf::from(tmp_file_name)),
                product: "zedboot".to_string(),
                ..Default::default()
            },
        )
        .await
    }
}
