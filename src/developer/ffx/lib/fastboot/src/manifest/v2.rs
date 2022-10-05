// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        common::{
            cmd::ManifestParams, crypto::unlock_device, file::FileResolver, finish,
            flash_bootloader, flash_product, is_locked, lock_device, verify_hardware, Boot, Flash,
            Unlock, MISSING_CREDENTIALS, MISSING_PRODUCT,
        },
        manifest::v1::FlashManifest as FlashManifestV1,
        unlock::unlock,
    },
    anyhow::Result,
    async_trait::async_trait,
    errors::ffx_bail,
    fidl_fuchsia_developer_ffx::FastbootProxy,
    serde::{Deserialize, Serialize},
    std::io::Write,
};

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct FlashManifest {
    pub hw_revision: String,
    #[serde(default)]
    pub credentials: Vec<String>,
    #[serde(rename = "products")]
    pub v1: FlashManifestV1,
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
        if !cmd.skip_verify {
            verify_hardware(&self.hw_revision, &fastboot_proxy).await?;
        }
        let product = match self.v1.0.iter().find(|product| product.name == cmd.product) {
            Some(res) => res,
            None => ffx_bail!("{} {}", MISSING_PRODUCT, cmd.product),
        };
        if product.requires_unlock && is_locked(&fastboot_proxy).await? {
            if self.credentials.len() == 0 {
                ffx_bail!("{}", MISSING_CREDENTIALS);
            } else {
                unlock_device(writer, file_resolver, &self.credentials, &fastboot_proxy).await?;
            }
        }
        flash_bootloader(writer, file_resolver, product, &fastboot_proxy, &cmd).await?;
        if product.requires_unlock && !is_locked(&fastboot_proxy).await? {
            lock_device(&fastboot_proxy).await?;
        }
        flash_product(writer, file_resolver, product, &fastboot_proxy, &cmd).await?;
        finish(writer, &fastboot_proxy).await
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
        unlock(writer, file_resolver, &self.credentials, &fastboot_proxy).await
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
        self.v1.boot(writer, file_resolver, slot, fastboot_proxy, cmd).await
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use crate::common::REVISION_VAR;
    use crate::common::{
        cmd::{BootParams, Command},
        IS_USERSPACE_VAR, LOCKED_VAR,
    };
    use crate::test::{setup, TestResolver};
    use serde_json::from_str;
    use std::path::PathBuf;
    use tempfile::NamedTempFile;

    const MANIFEST: &'static str = r#"{
        "hw_revision": "rev_test",
        "products": [
            {
                "name": "zedboot",
                "bootloader_partitions": [],
                "partitions": [
                    ["test1", "path1"],
                    ["test2", "path2"],
                    ["test3", "path3"],
                    ["test4", "path4"],
                    ["test5", "path5"]
                ],
                "oem_files": []
            }
        ]
    }"#;

    const MISMATCH_MANIFEST: &'static str = r#"{
        "hw_revision": "mismatch",
        "products": [
            {
                "name": "zedboot",
                "bootloader_partitions": [],
                "partitions": [
                    ["test1", "path1"],
                    ["test2", "path2"],
                    ["test3", "path3"],
                    ["test4", "path4"],
                    ["test5", "path5"]
                ],
                "oem_files": []
            }
        ]
    }"#;

    const NO_CREDS_MANIFEST: &'static str = r#"{
        "hw_revision": "zedboot",
        "products": [
            {
                "name": "zedboot",
                "requires_unlock": true,
                "bootloader_partitions": [],
                "partitions": [
                    ["test1", "path1"],
                    ["test2", "path2"],
                    ["test3", "path3"],
                    ["test4", "path4"],
                    ["test5", "path5"]
                ],
                "oem_files": []
            }
        ]
    }"#;

    const BOOTABLE_MANIFEST: &'static str = r#"{
        "hw_revision": "zedboot",
        "products": [
            {
                "name": "zedboot",
                "requires_unlock": false,
                "bootloader_partitions": [],
                "partitions": [
                    ["zircon_a", "path1"],
                    ["zircon_b", "path2"],
                    ["vbmeta_a", "path3"],
                    ["vbmeta_b", "path4"]
                ],
                "oem_files": []
            }
        ]
    }"#;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_matching_revision_should_work() -> Result<()> {
        let v: FlashManifest = from_str(MANIFEST)?;
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        let (state, proxy) = setup();
        {
            let mut state = state.lock().unwrap();
            state.set_var(IS_USERSPACE_VAR.to_string(), "yes".to_string());
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_mismatching_revision_should_err() -> Result<()> {
        let v: FlashManifest = from_str(MISMATCH_MANIFEST)?;
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        let (state, proxy) = setup();
        {
            let mut state = state.lock().unwrap();
            state.set_var(IS_USERSPACE_VAR.to_string(), "yes".to_string());
            state.set_var(REVISION_VAR.to_string(), "test".to_string());
        }
        let mut writer = Vec::<u8>::new();
        assert!(v
            .flash(
                &mut writer,
                &mut TestResolver::new(),
                proxy,
                ManifestParams {
                    manifest: Some(PathBuf::from(tmp_file_name)),
                    product: "zedboot".to_string(),
                    ..Default::default()
                }
            )
            .await
            .is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_no_creds_and_requires_unlock_should_err() -> Result<()> {
        let v: FlashManifest = from_str(NO_CREDS_MANIFEST)?;
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        let (state, proxy) = setup();
        {
            let mut state = state.lock().unwrap();
            state.set_var(IS_USERSPACE_VAR.to_string(), "no".to_string());
            state.set_var(REVISION_VAR.to_string(), "zedboot".to_string());
            state.set_var(LOCKED_VAR.to_string(), "yes".to_string());
        }
        let mut writer = Vec::<u8>::new();
        assert!(v
            .flash(
                &mut writer,
                &mut TestResolver::new(),
                proxy,
                ManifestParams {
                    manifest: Some(PathBuf::from(tmp_file_name)),
                    product: "zedboot".to_string(),
                    ..Default::default()
                }
            )
            .await
            .is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_boot_should_succeed() -> Result<()> {
        let v: FlashManifest = from_str(BOOTABLE_MANIFEST)?;
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        let (state, proxy) = setup();
        {
            let mut state = state.lock().unwrap();
            state.set_var(IS_USERSPACE_VAR.to_string(), "yes".to_string());
            state.set_var(REVISION_VAR.to_string(), "zedboot".to_string());
        }
        let mut writer = Vec::<u8>::new();
        v.flash(
            &mut writer,
            &mut TestResolver::new(),
            proxy,
            ManifestParams {
                manifest: Some(PathBuf::from(tmp_file_name)),
                product: "zedboot".to_string(),
                op: Command::Boot(BootParams { zbi: None, vbmeta: None, slot: "a".to_string() }),
                ..Default::default()
            },
        )
        .await
    }
}
