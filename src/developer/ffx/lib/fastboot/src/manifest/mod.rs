// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        common::{
            cmd::{BootParams, Command, ManifestParams},
            file::{ArchiveResolver, FileResolver, Resolver, TarResolver},
            gcs::GcsResolver,
            prepare, Boot, Flash, Unlock,
        },
        manifest::{
            sdk::SdkEntries, v1::FlashManifest as FlashManifestV1,
            v2::FlashManifest as FlashManifestV2, v3::FlashManifest as FlashManifestV3,
        },
    },
    anyhow::{anyhow, Context, Result},
    async_trait::async_trait,
    chrono::Utc,
    errors::{ffx_bail, ffx_error},
    fidl_fuchsia_developer_bridge::FastbootProxy,
    fms::Entries,
    sdk_metadata::{Metadata, ProductBundleV1},
    serde::{Deserialize, Serialize},
    serde_json::{from_value, Value},
    std::fs::File,
    std::io::{BufReader, Read, Write},
    std::path::PathBuf,
    termion::{color, style},
};

pub mod sdk;
pub mod v1;
pub mod v2;
pub mod v3;

pub const UNKNOWN_VERSION: &str = "Unknown flash manifest version";

#[derive(Default, Deserialize)]
pub struct Images(Vec<Image>);

#[derive(Default, Deserialize)]
pub struct Image {
    pub name: String,
    pub path: String,
    // Ignore the rest of the fields
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ManifestFile {
    version: u64,
    manifest: Value,
}

pub enum FlashManifestVersion {
    V1(FlashManifestV1),
    V2(FlashManifestV2),
    V3(FlashManifestV3),
    Sdk(SdkEntries),
}

impl FlashManifestVersion {
    pub fn load<R: Read>(reader: R) -> Result<Self> {
        let value: Value = serde_json::from_reader::<R, Value>(reader)
            .context("reading flash manifest from disk")?;
        // GN generated JSON always comes from a list
        let manifest: ManifestFile = match value {
            Value::Array(v) => from_value(v[0].clone())?,
            Value::Object(_) => from_value(value)?,
            _ => ffx_bail!("Could not parse flash manifest."),
        };
        match manifest.version {
            1 => Ok(Self::V1(from_value(manifest.manifest.clone())?)),
            2 => Ok(Self::V2(from_value(manifest.manifest.clone())?)),
            3 => Ok(Self::V3(from_value(manifest.manifest.clone())?)),
            _ => ffx_bail!("{}", UNKNOWN_VERSION),
        }
    }

    pub fn from_in_tree(path: PathBuf) -> Result<Self> {
        let mut entries = Entries::new();
        let mut path = match path.parent() {
            Some(p) => p.to_path_buf(),
            None => path.clone(),
        };
        let manifest_path = path.join("images.json");
        let images: Images = File::open(manifest_path.clone())
            .map_err(|e| ffx_error!("Cannot open file {:?} \nerror: {:?}", manifest_path, e))
            .map(BufReader::new)
            .map(serde_json::from_reader)?
            .map_err(|e| anyhow!("json parsing errored {}", e))?;
        let product_bundle =
            images.0.iter().find(|i| i.name == "product_bundle").map(|i| i.path.clone());
        if let Some(pb) = product_bundle {
            path.push(pb);
        } else {
            ffx_bail!("Could not find the Product Bundle in the SDK. Update your SDK and retry");
        }
        let file = File::open(path)?;
        entries.add_json(&mut BufReader::new(file))?;
        Ok(Self::Sdk(SdkEntries::new(entries)))
    }

    pub fn from_product_bundle(product_bundle: ProductBundleV1) -> Result<Self> {
        let mut entries = Entries::new();
        entries.add_metadata(Metadata::ProductBundleV1(product_bundle))?;
        Ok(Self::Sdk(SdkEntries::new(entries)))
    }
}

#[async_trait(?Send)]
impl Flash for FlashManifestVersion {
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
        let total_time = Utc::now();
        prepare(writer, &fastboot_proxy).await?;
        match self {
            Self::V1(v) => v.flash(writer, file_resolver, fastboot_proxy, cmd).await?,
            Self::V2(v) => v.flash(writer, file_resolver, fastboot_proxy, cmd).await?,
            Self::V3(v) => v.flash(writer, file_resolver, fastboot_proxy, cmd).await?,
            Self::Sdk(v) => v.flash(writer, file_resolver, fastboot_proxy, cmd).await?,
        };
        let duration = Utc::now().signed_duration_since(total_time);
        writeln!(
            writer,
            "{}Total Time{} [{}{:.2}s{}]",
            color::Fg(color::Green),
            style::Reset,
            color::Fg(color::Blue),
            (duration.num_milliseconds() as f32) / (1000 as f32),
            style::Reset
        )?;
        Ok(())
    }
}

#[async_trait(?Send)]
impl Unlock for FlashManifestVersion {
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
        let total_time = Utc::now();
        prepare(writer, &fastboot_proxy).await?;
        match self {
            Self::V1(v) => v.unlock(writer, file_resolver, fastboot_proxy).await?,
            Self::V2(v) => v.unlock(writer, file_resolver, fastboot_proxy).await?,
            Self::V3(v) => v.unlock(writer, file_resolver, fastboot_proxy).await?,
            Self::Sdk(v) => v.unlock(writer, file_resolver, fastboot_proxy).await?,
        };
        let duration = Utc::now().signed_duration_since(total_time);
        writeln!(
            writer,
            "{}Total Time{} [{}{:.2}s{}]",
            color::Fg(color::Green),
            style::Reset,
            color::Fg(color::Blue),
            (duration.num_milliseconds() as f32) / (1000 as f32),
            style::Reset
        )?;
        Ok(())
    }
}

#[async_trait(?Send)]
impl Boot for FlashManifestVersion {
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
        let total_time = Utc::now();
        prepare(writer, &fastboot_proxy).await?;
        match self {
            Self::V1(v) => v.boot(writer, file_resolver, slot, fastboot_proxy, cmd).await?,
            Self::V2(v) => v.boot(writer, file_resolver, slot, fastboot_proxy, cmd).await?,
            Self::V3(v) => v.boot(writer, file_resolver, slot, fastboot_proxy, cmd).await?,
            Self::Sdk(v) => v.boot(writer, file_resolver, slot, fastboot_proxy, cmd).await?,
        };
        let duration = Utc::now().signed_duration_since(total_time);
        writeln!(
            writer,
            "{}Total Time{} [{}{:.2}s{}]",
            color::Fg(color::Green),
            style::Reset,
            color::Fg(color::Blue),
            (duration.num_milliseconds() as f32) / (1000 as f32),
            style::Reset
        )?;
        Ok(())
    }
}

pub async fn from_sdk<W: Write>(
    writer: &mut W,
    version: String,
    fastboot_proxy: FastbootProxy,
    cmd: ManifestParams,
) -> Result<()> {
    match cmd.product_bundle.as_ref() {
        Some(b) => {
            let resolver = GcsResolver::new(version.clone(), b.to_string()).await?;
            let product_bundle = resolver.product_bundle().clone();
            FlashManifest {
                resolver,
                version: FlashManifestVersion::from_product_bundle(product_bundle)?,
            }
            .flash(writer, fastboot_proxy, cmd)
            .await
        }
        None => ffx_bail!(
            "Please supply the `--product-bundle` option to identify which product bundle to flash"
        ),
    }
}

pub async fn from_in_tree<W: Write>(
    writer: &mut W,
    path: PathBuf,
    fastboot_proxy: FastbootProxy,
    cmd: ManifestParams,
) -> Result<()> {
    FlashManifest {
        resolver: Resolver::new(path.clone())?,
        version: FlashManifestVersion::from_in_tree(path.clone())?,
    }
    .flash(writer, fastboot_proxy, cmd)
    .await
}

pub async fn from_path<W: Write>(
    writer: &mut W,
    path: PathBuf,
    fastboot_proxy: FastbootProxy,
    cmd: ManifestParams,
) -> Result<()> {
    match path.extension() {
        Some(ext) => {
            if ext == "zip" {
                let r = ArchiveResolver::new(writer, path)?;
                load_flash_manifest(r)?.flash(writer, fastboot_proxy, cmd).await
            } else if ext == "tgz" || ext == "tar.gz" || ext == "tar" {
                let r = TarResolver::new(writer, path)?;
                load_flash_manifest(r)?.flash(writer, fastboot_proxy, cmd).await
            } else {
                load_flash_manifest(Resolver::new(path)?)?.flash(writer, fastboot_proxy, cmd).await
            }
        }
        _ => load_flash_manifest(Resolver::new(path)?)?.flash(writer, fastboot_proxy, cmd).await,
    }
}

fn load_flash_manifest<F: FileResolver + Sync>(
    file_resolver: F,
) -> Result<FlashManifest<impl FileResolver + Sync>> {
    let reader = File::open(file_resolver.manifest()).map(BufReader::new)?;
    Ok(FlashManifest { resolver: file_resolver, version: FlashManifestVersion::load(reader)? })
}

pub struct FlashManifest<F: FileResolver + Sync> {
    resolver: F,
    version: FlashManifestVersion,
}

impl<F: FileResolver + Sync> FlashManifest<F> {
    pub async fn flash<W: Write>(
        &mut self,
        writer: &mut W,
        fastboot_proxy: FastbootProxy,
        cmd: ManifestParams,
    ) -> Result<()> {
        match &cmd.op {
            Command::Flash => {
                self.version.flash(writer, &mut self.resolver, fastboot_proxy, cmd).await
            }
            Command::Unlock(_) => {
                // Using the manifest, don't need the unlock credential from the UnlockCommand
                // here.
                self.version.unlock(writer, &mut self.resolver, fastboot_proxy).await
            }
            Command::Boot(BootParams { slot, .. }) => {
                self.version
                    .boot(writer, &mut self.resolver, slot.to_owned(), fastboot_proxy, cmd)
                    .await
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use serde_json::from_str;
    use std::io::BufReader;

    const UNKNOWN_VERSION: &'static str = r#"{
        "version": 99999,
        "manifest": "test"
    }"#;

    const MANIFEST: &'static str = r#"{
        "version": 1,
        "manifest": []
    }"#;

    const ARRAY_MANIFEST: &'static str = r#"[{
        "version": 1,
        "manifest": []
    }]"#;

    #[test]
    fn test_deserialization() -> Result<()> {
        let _manifest: ManifestFile = from_str(MANIFEST)?;
        Ok(())
    }

    #[test]
    fn test_loading_unknown_version() {
        let manifest_contents = UNKNOWN_VERSION.to_string();
        let result = FlashManifestVersion::load(BufReader::new(manifest_contents.as_bytes()));
        assert!(result.is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_loading_version_1() -> Result<()> {
        let manifest_contents = MANIFEST.to_string();
        FlashManifestVersion::load(BufReader::new(manifest_contents.as_bytes())).map(|_| ())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_loading_version_1_from_array() -> Result<()> {
        let manifest_contents = ARRAY_MANIFEST.to_string();
        FlashManifestVersion::load(BufReader::new(manifest_contents.as_bytes())).map(|_| ())
    }
}
