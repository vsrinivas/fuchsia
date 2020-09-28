// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    async_trait::async_trait,
    ffx_core::ffx_bail,
    ffx_flash_args::FlashCommand,
    fidl_fuchsia_developer_bridge::FastbootProxy,
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
    pub(crate) partitions: Vec<Partition>,
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
        fastboot_proxy
            .erase("misc")
            .await?
            .map_err(|_| anyhow!("Could not erase misc partition"))?;
        fastboot_proxy.reboot().await?.map_err(|_| anyhow!("Could not reboot device"))?;
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
                "partitions": [
                    ["test1", "path1"],
                    ["test2", "path2"],
                    ["test3", "path3"],
                    ["test4", "path4"],
                    ["test5", "path5"]
                ]
            },
            {
                "name": "product", 
                "partitions": [
                    ["test10", "path10"],
                    ["test20", "path20"],
                    ["test30", "path30"]
                ]
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
            let product: &Product = &v.0[1];
            assert_eq!("product", product.name);
            assert_eq!(3, product.partitions.len());
            let expected2 = [["test10", "path10"], ["test20", "path20"], ["test30", "path30"]];
            for x in 0..expected2.len() {
                assert_eq!(product.partitions[x].name(), expected2[x][0]);
                assert_eq!(product.partitions[x].file(), expected2[x][1]);
            }
        } else {
            bail!("Parsed incorrect version");
        }
        Ok(())
    }
}
