// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        file::FileResolver,
        manifest::{
            v3::{
                Condition as ConditionV3, ExplicitOemFile as OemFileV3,
                FlashManifest as FlashManifestV3, Partition as PartitionV3, Product as ProductV3,
            },
            Flash,
        },
    },
    anyhow::{bail, Result},
    async_trait::async_trait,
    ffx_flash_args::FlashCommand,
    fidl_fuchsia_developer_bridge::FastbootProxy,
    fms::Entries,
    sdk_metadata::{Metadata, OemFile, Partition, Product},
    std::convert::{From, TryFrom, TryInto},
    std::default::Default,
    std::io::Write,
};

// Wrapper struct so we can convert to a FlashManifest
pub struct SdkEntries {
    entries: Entries,
}

impl SdkEntries {
    pub(crate) fn new(entries: Entries) -> Self {
        Self { entries }
    }
}

impl TryFrom<&SdkEntries> for FlashManifestV3 {
    type Error = anyhow::Error;
    fn try_from(p: &SdkEntries) -> Result<FlashManifestV3> {
        // Eventually we'll need to use the identifier to get an entry.
        match p.entries.iter().next() {
            Some(Metadata::ProductBundleV1(bundle)) => match &bundle.manifests {
                Some(m) => match &m.flash {
                    Some(f) => Ok(FlashManifestV3 {
                        hw_revision: f.hw_revision.clone(),
                        products: f.products.iter().map(|p| p.into()).collect(),
                    }),
                    None => bail!("SDK Flash Manifest does not exist"),
                },
                None => bail!("No manifests in product bundle"),
            },
            _ => bail!("SDK Flash Manifest is malformed"),
        }
    }
}

impl From<&Product> for ProductV3 {
    fn from(p: &Product) -> ProductV3 {
        ProductV3 {
            name: p.name.clone(),
            bootloader_partitions: p.bootloader_partitions.iter().map(|p| p.into()).collect(),
            partitions: p.partitions.iter().map(|p| p.into()).collect(),
            oem_files: p.oem_files.iter().map(|f| f.into()).collect(),
            ..Default::default()
        }
    }
}

impl From<&Partition> for PartitionV3 {
    fn from(p: &Partition) -> PartitionV3 {
        PartitionV3 {
            name: p.name.clone(),
            path: p.path.clone(),
            condition: p
                .condition
                .as_ref()
                .map(|c| ConditionV3 { variable: c.variable.clone(), value: c.value.clone() }),
        }
    }
}

impl From<&OemFile> for OemFileV3 {
    fn from(f: &OemFile) -> OemFileV3 {
        OemFileV3 { command: f.command.clone(), path: f.path.clone() }
    }
}

#[async_trait(?Send)]
impl Flash for SdkEntries {
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
        let v3: FlashManifestV3 = self.try_into()?;
        v3.flash(writer, file_resolver, fastboot_proxy, cmd).await
    }
}
