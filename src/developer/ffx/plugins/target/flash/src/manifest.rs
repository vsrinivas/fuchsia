// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Result},
    async_trait::async_trait,
    ffx_core::ffx_bail,
    ffx_flash_args::FlashCommand,
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_developer_bridge::{FastbootProxy, RebootListenerMarker, RebootListenerRequest},
    futures::prelude::*,
    futures::try_join,
    serde::{Deserialize, Serialize},
    serde_json::{from_value, Value},
    std::io::{Read, Write},
};

pub(crate) const UNKNOWN_VERSION: &str = "Unknown flash manifest version";
pub(crate) const MISSING_PRODUCT: &str = "Manifest does not contain product";
pub(crate) const MULTIPLE_PRODUCT: &str =
    "Multiple products found in manifest. Please specify a product";

#[async_trait]
pub(crate) trait Flash {
    async fn flash<W>(
        &self,
        writer: W,
        fastboot_proxy: FastbootProxy,
        cmd: FlashCommand,
    ) -> Result<()>
    where
        W: Write + Send;
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub(crate) struct ManifestFile {
    version: u64,
    manifest: Value,
}

pub(crate) enum FlashManifest {
    V1(FlashManifestV1),
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub(crate) struct FlashManifestV1(pub(crate) Vec<Product>);

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub(crate) struct Product {
    pub(crate) name: String,
    pub(crate) bootloader_partitions: Vec<Partition>,
    pub(crate) partitions: Vec<Partition>,
    pub(crate) oem_files: Vec<OemFile>,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub(crate) struct Partition(String, String);

impl Partition {
    pub(crate) fn name(&self) -> &str {
        self.0.as_str()
    }

    pub(crate) fn file(&self) -> &str {
        self.1.as_str()
    }
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub(crate) struct OemFile(String, String);

impl OemFile {
    pub(crate) fn command(&self) -> &str {
        self.0.as_str()
    }

    pub(crate) fn file(&self) -> &str {
        self.1.as_str()
    }
}

impl FlashManifest {
    pub(crate) fn load<R: Read>(reader: R) -> Result<Self> {
        let manifest: ManifestFile = serde_json::from_reader::<R, ManifestFile>(reader)
            .context("reading flash manifest from disk")?;
        match manifest.version {
            1 => Ok(Self::V1(from_value(manifest.manifest.clone())?)),
            _ => ffx_bail!("{}", UNKNOWN_VERSION),
        }
    }
}

#[async_trait]
impl Flash for FlashManifest {
    async fn flash<W>(
        &self,
        writer: W,
        fastboot_proxy: FastbootProxy,
        cmd: FlashCommand,
    ) -> Result<()>
    where
        W: Write + Send,
    {
        match self {
            Self::V1(v) => v.flash(writer, fastboot_proxy, cmd).await,
        }
    }
}

#[async_trait]
impl Flash for FlashManifestV1 {
    async fn flash<W>(
        &self,
        mut writer: W,
        fastboot_proxy: FastbootProxy,
        cmd: FlashCommand,
    ) -> Result<()>
    where
        W: Write + Send,
    {
        let product = match cmd.product {
            Some(p) => {
                if let Some(res) = self.0.iter().find(|product| product.name == p) {
                    res
                } else {
                    ffx_bail!("{} {}", MISSING_PRODUCT, p);
                }
            }
            None => {
                if self.0.len() == 1 {
                    &self.0[0]
                } else {
                    ffx_bail!("{}", MULTIPLE_PRODUCT);
                }
            }
        };
        for partition in &product.bootloader_partitions {
            writeln!(writer, "Writing \"{}\" from {}", partition.name(), partition.file())?;
            //TODO: better error handling when errors are well defined.
            fastboot_proxy.flash(partition.name(), partition.file()).await?.map_err(|_| {
                anyhow!(
                    "There was an error flashing \"{}\" - {}",
                    partition.name(),
                    partition.file()
                )
            })?;
        }
        if product.bootloader_partitions.len() > 0 {
            writeln!(writer, "Rebooting to bootloader")?;
            let (reboot_client, reboot_server) = create_endpoints::<RebootListenerMarker>()?;
            let mut stream = reboot_server.into_stream()?;
            try_join!(
                fastboot_proxy
                    .reboot_bootloader(reboot_client)
                    .map_err(|e| anyhow!("fidl error when rebooting to bootloader: {:?}", e)),
                async move {
                    if let Some(RebootListenerRequest::OnReboot { control_handle: _ }) =
                        stream.try_next().await?
                    {
                        log::info!("Received reboot signal");
                        Ok(())
                    } else {
                        bail!("Did not receive reboot signal");
                    }
                }
            )
            .map(|(reboot, _)| {
                reboot.map_err(|e| anyhow!("failed booting to bootloader: {:?}", e)).unwrap()
            })?;
        }
        for partition in &product.partitions {
            writeln!(writer, "Writing \"{}\" from {}", partition.name(), partition.file())?;
            //TODO: better error handling when errors are well defined.
            fastboot_proxy.flash(partition.name(), partition.file()).await?.map_err(|_| {
                anyhow!(
                    "There was an error flashing \"{}\" - {}",
                    partition.name(),
                    partition.file()
                )
            })?;
        }
        for oem_file in &product.oem_files {
            writeln!(writer, "Writing {}", oem_file.file())?;
            //TODO: better error handling when errors are well defined.
            fastboot_proxy
                .stage(oem_file.file())
                .await?
                .map_err(|_| anyhow!("There was an error staging {}", oem_file.file()))?;
            writeln!(writer, "Sending command \"{}\"", oem_file.command())?;
            fastboot_proxy.oem(oem_file.command()).await?.map_err(|_| {
                anyhow!("There was an error sending oem command \"{}\"", oem_file.command(),)
            })?;
        }
        fastboot_proxy
            .erase("misc")
            .await?
            .map_err(|_| anyhow!("Could not erase misc partition"))?;
        fastboot_proxy.set_active("a").await?.map_err(|_| anyhow!("Could not set active slot"))?;
        fastboot_proxy.continue_boot().await?.map_err(|_| anyhow!("Could not reboot device"))?;
        writeln!(writer, "Continuing to boot - this could take awhile")?;
        Ok(())
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use anyhow::bail;
    use serde_json::from_str;
    use std::io::BufReader;

    const UNKNOWN_VERSION: &'static str = r#"{
        "version": 99999,
        "manifest": "test"
    }"#;

    const MANIFEST: &'static str = r#"{
        "version": 1,
        "manifest": [ 
            {
                "name": "zedboot", 
                "bootloader_partitions": [
                    ["test1", "path1"],
                    ["test2", "path2"]
                ],
                "partitions": [
                    ["test1", "path1"],
                    ["test2", "path2"],
                    ["test3", "path3"],
                    ["test4", "path4"],
                    ["test5", "path5"]
                ],
                "oem_files": [
                    ["test1", "path1"],
                    ["test2", "path2"]
                ] 
            },
            {
                "name": "product", 
                "bootloader_partitions": [],
                "partitions": [
                    ["test10", "path10"],
                    ["test20", "path20"],
                    ["test30", "path30"]
                ],
                "oem_files": []
            }
        ]
    }"#;

    #[test]
    fn test_deserialization() -> Result<()> {
        let _manifest: ManifestFile = from_str(MANIFEST)?;
        Ok(())
    }

    #[test]
    fn test_loading_unknown_version() {
        let manifest_contents = UNKNOWN_VERSION.to_string();
        let result = FlashManifest::load(BufReader::new(manifest_contents.as_bytes()));
        assert!(result.is_err());
    }

    #[allow(irrefutable_let_patterns)]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_loading_version_1() -> Result<()> {
        let manifest_contents = MANIFEST.to_string();
        let manifest = FlashManifest::load(BufReader::new(manifest_contents.as_bytes()))?;
        if let FlashManifest::V1(v) = manifest {
            let zedboot: &Product = &v.0[0];
            assert_eq!("zedboot", zedboot.name);
            assert_eq!(2, zedboot.bootloader_partitions.len());
            let bootloader_expected = [["test1", "path1"], ["test2", "path2"]];
            for x in 0..bootloader_expected.len() {
                assert_eq!(zedboot.bootloader_partitions[x].name(), bootloader_expected[x][0]);
                assert_eq!(zedboot.bootloader_partitions[x].file(), bootloader_expected[x][1]);
            }
            assert_eq!(5, zedboot.partitions.len());
            let expected = [
                ["test1", "path1"],
                ["test2", "path2"],
                ["test3", "path3"],
                ["test4", "path4"],
                ["test5", "path5"],
            ];
            for x in 0..expected.len() {
                assert_eq!(zedboot.partitions[x].name(), expected[x][0]);
                assert_eq!(zedboot.partitions[x].file(), expected[x][1]);
            }
            assert_eq!(2, zedboot.oem_files.len());
            let oem_files_expected = [["test1", "path1"], ["test2", "path2"]];
            for x in 0..oem_files_expected.len() {
                assert_eq!(zedboot.oem_files[x].command(), oem_files_expected[x][0]);
                assert_eq!(zedboot.oem_files[x].file(), oem_files_expected[x][1]);
            }
            let product: &Product = &v.0[1];
            assert_eq!("product", product.name);
            assert_eq!(0, product.bootloader_partitions.len());
            assert_eq!(3, product.partitions.len());
            let expected2 = [["test10", "path10"], ["test20", "path20"], ["test30", "path30"]];
            for x in 0..expected2.len() {
                assert_eq!(product.partitions[x].name(), expected2[x][0]);
                assert_eq!(product.partitions[x].file(), expected2[x][1]);
            }
            assert_eq!(0, product.oem_files.len());
        } else {
            bail!("Parsed incorrect version");
        }
        Ok(())
    }
}
