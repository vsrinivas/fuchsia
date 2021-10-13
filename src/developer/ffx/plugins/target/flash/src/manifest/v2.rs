// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        file::FileResolver,
        manifest::{
            flash_and_reboot, get_unlock_challenge, is_locked,
            v1::FlashManifest as FlashManifestV1, verify_hardware, Flash, MISSING_PRODUCT,
            UNLOCK_ERR,
        },
    },
    anyhow::Result,
    async_trait::async_trait,
    errors::ffx_bail,
    ffx_flash_args::FlashCommand,
    fidl_fuchsia_developer_bridge::FastbootProxy,
    serde::{Deserialize, Serialize},
    std::io::Write,
};

const MISSING_CREDENTIALS: &str =
    "The flash manifest is missing the credential files to unlock this device.\n\
     Please unlock the target and try again.";

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub(crate) struct FlashManifest {
    pub(crate) hw_revision: String,
    #[serde(default)]
    pub(crate) credentials: Vec<String>,
    #[serde(rename = "products")]
    pub(crate) v1: FlashManifestV1,
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
                //TODO: Try unlock the device.
                //TODO: Match the unlock challenge to the data in self.credentials
                let _unlock = get_unlock_challenge(&fastboot_proxy).await?;
                ffx_bail!("{}", UNLOCK_ERR);
            }
        }
        flash_and_reboot(writer, file_resolver, product, &fastboot_proxy, cmd).await
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
                "requires_unlock": false,
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_matching_revision_should_work() -> Result<()> {
        let v: FlashManifest = from_str(MANIFEST)?;
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
    async fn test_mismatching_revision_should_err() -> Result<()> {
        let v: FlashManifest = from_str(MISMATCH_MANIFEST)?;
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        let (_, proxy) = setup();
        let mut writer = Vec::<u8>::new();
        assert!(v
            .flash(
                &mut writer,
                &mut TestResolver::new(),
                proxy,
                FlashCommand {
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
        let (_, proxy) = setup();
        let mut writer = Vec::<u8>::new();
        assert!(v
            .flash(
                &mut writer,
                &mut TestResolver::new(),
                proxy,
                FlashCommand {
                    manifest: Some(PathBuf::from(tmp_file_name)),
                    product: "zedboot".to_string(),
                    ..Default::default()
                }
            )
            .await
            .is_err());
        Ok(())
    }
}
