// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        common::{
            cmd::{BootParams, Command, ManifestParams},
            file::{ArchiveResolver, FileResolver, Resolver, TarResolver},
            prepare, Boot, Flash, Unlock,
        },
        manifest::{
            sdk::SdkEntries, v1::FlashManifest as FlashManifestV1,
            v2::FlashManifest as FlashManifestV2, v3::FlashManifest as FlashManifestV3,
        },
    },
    anyhow::{anyhow, Context, Result},
    assembly_manifest::AssemblyManifest,
    assembly_partitions_config::{Partition, Slot},
    assembly_util::PathToStringExt,
    async_trait::async_trait,
    chrono::Utc,
    errors::{ffx_bail, ffx_error},
    fidl_fuchsia_developer_ffx::FastbootProxy,
    fms::Entries,
    pbms::{load_product_bundle, ListingMode},
    sdk_metadata::{Metadata, ProductBundle, ProductBundleV2},
    serde::{Deserialize, Serialize},
    serde_json::{from_value, to_value, Value},
    std::collections::BTreeMap,
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
    manifest: Value,
    version: u64,
}

pub enum FlashManifestVersion {
    V1(FlashManifestV1),
    V2(FlashManifestV2),
    V3(FlashManifestV3),
    Sdk(SdkEntries),
}

/// The type of the image used in the below ImageMap.
#[derive(Debug, PartialOrd, Ord, PartialEq, Eq)]
enum ImageType {
    ZBI,
    VBMeta,
    FVM,
}

/// A map from a slot to the image paths assigned to that slot.
/// This is used during the construction of a manifest from a product bundle.
type ImageMap = BTreeMap<Slot, BTreeMap<ImageType, String>>;

impl FlashManifestVersion {
    pub fn write<W: Write>(&self, writer: W) -> Result<()> {
        let manifest = match &self {
            FlashManifestVersion::V1(manifest) => {
                ManifestFile { version: 1, manifest: to_value(manifest)? }
            }
            FlashManifestVersion::V2(manifest) => {
                ManifestFile { version: 2, manifest: to_value(manifest)? }
            }
            FlashManifestVersion::V3(manifest) => {
                ManifestFile { version: 3, manifest: to_value(manifest)? }
            }
            _ => ffx_bail!("{}", UNKNOWN_VERSION),
        };
        serde_json::to_writer_pretty(writer, &manifest).context("writing flash manifest")
    }

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

    pub fn from_product_bundle(product_bundle: &ProductBundle) -> Result<Self> {
        match product_bundle {
            ProductBundle::V1(product_bundle) => {
                let mut entries = Entries::new();
                entries.add_metadata(Metadata::ProductBundleV1(product_bundle.clone()))?;
                Ok(Self::Sdk(SdkEntries::new(entries)))
            }
            ProductBundle::V2(product_bundle) => Self::from_product_bundle_v2(product_bundle),
        }
    }

    fn from_product_bundle_v2(product_bundle: &ProductBundleV2) -> Result<Self> {
        // Copy the unlock credentials from the partitions config to the flash manifest.
        let mut credentials = vec![];
        for c in &product_bundle.partitions.unlock_credentials {
            credentials.push(c.path_to_string()?);
        }

        // Copy the bootloader partitions from the partitions config to the flash manifest.
        let mut bootloader_partitions = vec![];
        for p in &product_bundle.partitions.bootloader_partitions {
            if let Some(name) = &p.name {
                bootloader_partitions.push(v3::Partition {
                    name: name.to_string(),
                    path: p.image.path_to_string()?,
                    condition: None,
                });
            }
        }

        // Copy the bootloader partitions from the partitions config to the flash manifest. Join them
        // with the bootloader partitions, because they are always flashed together.
        let mut all_bootloader_partitions = bootloader_partitions.clone();
        for p in &product_bundle.partitions.bootstrap_partitions {
            let condition = if let Some(c) = &p.condition {
                Some(v3::Condition { variable: c.variable.to_string(), value: c.value.to_string() })
            } else {
                None
            };
            all_bootloader_partitions.push(v3::Partition {
                name: p.name.to_string(),
                path: p.image.path_to_string()?,
                condition,
            });
        }

        // Create a map from slot to available images by name (zbi, vbmeta, fvm).
        let mut image_map: ImageMap = BTreeMap::new();
        if let Some(manifest) = &product_bundle.system_a {
            add_images_to_map(&mut image_map, &manifest, Slot::A)?;
        }
        if let Some(manifest) = &product_bundle.system_b {
            add_images_to_map(&mut image_map, &manifest, Slot::B)?;
        }
        if let Some(manifest) = &product_bundle.system_r {
            add_images_to_map(&mut image_map, &manifest, Slot::R)?;
        }

        // Define the flashable "products".
        let mut products = vec![];
        products.push(v3::Product {
            name: "recovery".into(),
            bootloader_partitions: bootloader_partitions.clone(),
            partitions: get_mapped_partitions(
                &product_bundle.partitions.partitions,
                &image_map,
                /*is_recovery=*/ true,
            ),
            oem_files: vec![],
            requires_unlock: false,
        });
        products.push(v3::Product {
            name: "fuchsia_only".into(),
            bootloader_partitions: bootloader_partitions.clone(),
            partitions: get_mapped_partitions(
                &product_bundle.partitions.partitions,
                &image_map,
                /*is_recovery=*/ false,
            ),
            oem_files: vec![],
            requires_unlock: false,
        });
        products.push(v3::Product {
            name: "fuchsia".into(),
            bootloader_partitions: all_bootloader_partitions.clone(),
            partitions: get_mapped_partitions(
                &product_bundle.partitions.partitions,
                &image_map,
                /*is_recovery=*/ false,
            ),
            oem_files: vec![],
            requires_unlock: !product_bundle.partitions.bootstrap_partitions.is_empty(),
        });
        if !product_bundle.partitions.bootstrap_partitions.is_empty() {
            products.push(v3::Product {
                name: "bootstrap".into(),
                bootloader_partitions: all_bootloader_partitions.clone(),
                partitions: vec![],
                oem_files: vec![],
                requires_unlock: true,
            });
        }

        // Create the flash manifest.
        let ret = v3::FlashManifest {
            hw_revision: product_bundle.partitions.hardware_revision.clone(),
            credentials,
            products,
        };

        Ok(Self::V3(ret))
    }
}

/// Add a set of images from |manifest| to the |image_map|, assigning them to |slot|. This ignores
/// all images other than the ZBI, VBMeta, and fastboot FVM.
fn add_images_to_map(
    image_map: &mut ImageMap,
    manifest: &AssemblyManifest,
    slot: Slot,
) -> Result<()> {
    let slot_entry = image_map.entry(slot).or_insert(BTreeMap::new());
    for image in &manifest.images {
        match image {
            assembly_manifest::Image::ZBI { path, .. } => {
                slot_entry.insert(ImageType::ZBI, path.path_to_string()?)
            }
            assembly_manifest::Image::VBMeta(path) => {
                slot_entry.insert(ImageType::VBMeta, path.path_to_string()?)
            }
            assembly_manifest::Image::FVMFastboot(path) => {
                if let Slot::R = slot {
                    // Recovery should not include a separate FVM, because it is embedded into the
                    // ZBI as a ramdisk.
                    None
                } else {
                    slot_entry.insert(ImageType::FVM, path.path_to_string()?)
                }
            }
            _ => None,
        };
    }
    Ok(())
}

/// Construct a list of partitions to add to the flash manifest by mapping the partitions to the
/// images. If |is_recovery|, then put the recovery images in every slot.
fn get_mapped_partitions(
    partitions: &Vec<Partition>,
    image_map: &ImageMap,
    is_recovery: bool,
) -> Vec<v3::Partition> {
    let mut mapped_partitions = vec![];

    // Assign the images to particular partitions. If |is_recovery|, then we use the recovery
    // images for all slots.
    for p in partitions {
        let (partition, name, slot) = match p {
            Partition::ZBI { name, slot } => (name, ImageType::ZBI, slot),
            Partition::VBMeta { name, slot } => (name, ImageType::VBMeta, slot),

            // Arbitrarily, take the fvm from the slot A system.
            Partition::FVM { name } => (name, ImageType::FVM, &Slot::A),
        };

        if let Some(slot) = match is_recovery {
            // If this is recovery mode, then fill every partition with images from the slot R
            // system.
            true => image_map.get(&Slot::R),
            false => image_map.get(slot),
        } {
            if let Some(path) = slot.get(&name) {
                mapped_partitions.push(v3::Partition {
                    name: partition.to_string(),
                    path: path.to_string(),
                    condition: None,
                });
            }
        }
    }

    mapped_partitions
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
    fastboot_proxy: FastbootProxy,
    cmd: ManifestParams,
) -> Result<()> {
    tracing::debug!("fastboot manifest from_sdk");
    match cmd.product_bundle.as_ref() {
        Some(b) => {
            let product_bundle =
                load_product_bundle(&Some(b.to_string()), ListingMode::AllBundles).await?;
            FlashManifest {
                resolver: Resolver::new(PathBuf::from(b))?,
                version: FlashManifestVersion::from_product_bundle(&product_bundle)?,
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
    tracing::debug!("fastboot manifest from_in_tree");
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
    tracing::debug!("fastboot manifest from_path");
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
    use maplit::btreemap;
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
    fn test_serialization() -> Result<()> {
        let manifest = FlashManifestVersion::V3(FlashManifestV3 {
            hw_revision: "board".into(),
            credentials: vec![],
            products: vec![],
        });
        let mut buf = Vec::new();
        manifest.write(&mut buf).unwrap();
        let str = String::from_utf8(buf).unwrap();
        assert_eq!(
            str,
            r#"{
  "manifest": {
    "hw_revision": "board"
  },
  "version": 3
}"#
        );
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

    #[test]
    fn test_add_images_to_map() {
        let mut image_map: ImageMap = BTreeMap::new();
        let manifest = AssemblyManifest {
            images: vec![
                assembly_manifest::Image::ZBI { path: "path/to/fuchsia.zbi".into(), signed: false },
                assembly_manifest::Image::VBMeta("path/to/fuchsia.vbmeta".into()),
                assembly_manifest::Image::FVMFastboot("path/to/fvm.fastboot.blk".into()),
                // These should be ignored.
                assembly_manifest::Image::FVM("path/to/fvm.blk".into()),
                assembly_manifest::Image::BasePackage("path/to/base".into()),
            ],
        };
        add_images_to_map(&mut image_map, &manifest, Slot::A).unwrap();
        assert_eq!(image_map.len(), 1);
        assert_eq!(image_map[&Slot::A].len(), 3);
        assert_eq!(image_map[&Slot::A][&ImageType::ZBI], "path/to/fuchsia.zbi");
        assert_eq!(image_map[&Slot::A][&ImageType::VBMeta], "path/to/fuchsia.vbmeta");
        assert_eq!(image_map[&Slot::A][&ImageType::FVM], "path/to/fvm.fastboot.blk");

        add_images_to_map(&mut image_map, &manifest, Slot::B).unwrap();
        assert_eq!(image_map.len(), 2);
        assert_eq!(image_map[&Slot::B].len(), 3);
        assert_eq!(image_map[&Slot::B][&ImageType::ZBI], "path/to/fuchsia.zbi");
        assert_eq!(image_map[&Slot::B][&ImageType::VBMeta], "path/to/fuchsia.vbmeta");
        assert_eq!(image_map[&Slot::B][&ImageType::FVM], "path/to/fvm.fastboot.blk");

        add_images_to_map(&mut image_map, &manifest, Slot::R).unwrap();
        assert_eq!(image_map.len(), 3);
        assert_eq!(image_map[&Slot::R].len(), 2);
        assert_eq!(image_map[&Slot::R][&ImageType::ZBI], "path/to/fuchsia.zbi");
        assert_eq!(image_map[&Slot::R][&ImageType::VBMeta], "path/to/fuchsia.vbmeta");
    }

    #[test]
    fn test_get_mapped_partitions_no_slots() {
        let partitions = vec![];
        let image_map: ImageMap = btreemap! {
            Slot::A => btreemap!{
                ImageType::ZBI => "zbi".into(),
                ImageType::VBMeta => "vbmeta".into(),
                ImageType::FVM => "fvm".into(),
            },
        };
        let mapped = get_mapped_partitions(&partitions, &image_map, /*is_recovery=*/ false);
        assert!(mapped.is_empty());
    }

    #[test]
    fn test_get_mapped_partitions_slot_a_only() {
        let partitions = vec![
            Partition::ZBI { name: "part1".into(), slot: Slot::A },
            Partition::VBMeta { name: "part2".into(), slot: Slot::A },
            Partition::FVM { name: "part3".into() },
        ];
        let image_map: ImageMap = btreemap! {
            Slot::A => btreemap!{
                ImageType::ZBI => "zbi_a".into(),
                ImageType::VBMeta => "vbmeta_a".into(),
                ImageType::FVM => "fvm_a".into(),
            },
            Slot::B => btreemap!{
                ImageType::ZBI => "zbi_b".into(),
                ImageType::VBMeta => "vbmeta_b".into(),
                ImageType::FVM => "fvm_b".into(),
            },
            Slot::R => btreemap!{
                ImageType::ZBI => "zbi_r".into(),
                ImageType::VBMeta => "vbmeta_r".into(),
            },
        };
        let mapped = get_mapped_partitions(&partitions, &image_map, /*is_recovery=*/ false);
        assert_eq!(
            mapped,
            vec![
                v3::Partition { name: "part1".into(), path: "zbi_a".into(), condition: None },
                v3::Partition { name: "part2".into(), path: "vbmeta_a".into(), condition: None },
                v3::Partition { name: "part3".into(), path: "fvm_a".into(), condition: None },
            ]
        );
    }

    #[test]
    fn test_get_mapped_partitions_all_slots() {
        let partitions = vec![
            Partition::ZBI { name: "part1".into(), slot: Slot::A },
            Partition::VBMeta { name: "part2".into(), slot: Slot::A },
            Partition::ZBI { name: "part3".into(), slot: Slot::B },
            Partition::VBMeta { name: "part4".into(), slot: Slot::B },
            Partition::ZBI { name: "part5".into(), slot: Slot::R },
            Partition::VBMeta { name: "part6".into(), slot: Slot::R },
            Partition::FVM { name: "part7".into() },
        ];
        let image_map: ImageMap = btreemap! {
            Slot::A => btreemap!{
                ImageType::ZBI => "zbi_a".into(),
                ImageType::VBMeta => "vbmeta_a".into(),
                ImageType::FVM => "fvm_a".into(),
            },
            Slot::B => btreemap!{
                ImageType::ZBI => "zbi_b".into(),
                ImageType::VBMeta => "vbmeta_b".into(),
                ImageType::FVM => "fvm_b".into(),
            },
            Slot::R => btreemap!{
                ImageType::ZBI => "zbi_r".into(),
                ImageType::VBMeta => "vbmeta_r".into(),
                ImageType::FVM => "fvm_r".into(),
            },
        };
        let mapped = get_mapped_partitions(&partitions, &image_map, /*is_recovery=*/ false);
        assert_eq!(
            mapped,
            vec![
                v3::Partition { name: "part1".into(), path: "zbi_a".into(), condition: None },
                v3::Partition { name: "part2".into(), path: "vbmeta_a".into(), condition: None },
                v3::Partition { name: "part3".into(), path: "zbi_b".into(), condition: None },
                v3::Partition { name: "part4".into(), path: "vbmeta_b".into(), condition: None },
                v3::Partition { name: "part5".into(), path: "zbi_r".into(), condition: None },
                v3::Partition { name: "part6".into(), path: "vbmeta_r".into(), condition: None },
                v3::Partition { name: "part7".into(), path: "fvm_a".into(), condition: None },
            ]
        );
    }

    #[test]
    fn test_get_mapped_partitions_missing_slot() {
        let partitions = vec![
            Partition::ZBI { name: "part1".into(), slot: Slot::A },
            Partition::VBMeta { name: "part2".into(), slot: Slot::A },
            Partition::ZBI { name: "part3".into(), slot: Slot::B },
            Partition::VBMeta { name: "part4".into(), slot: Slot::B },
            Partition::ZBI { name: "part5".into(), slot: Slot::R },
            Partition::VBMeta { name: "part6".into(), slot: Slot::R },
            Partition::FVM { name: "part7".into() },
        ];
        let image_map: ImageMap = btreemap! {
            Slot::A => btreemap!{
                ImageType::ZBI => "zbi_a".into(),
                ImageType::VBMeta => "vbmeta_a".into(),
                ImageType::FVM => "fvm_a".into(),
            },
            Slot::R => btreemap!{
                ImageType::ZBI => "zbi_r".into(),
                ImageType::VBMeta => "vbmeta_r".into(),
            },
        };
        let mapped = get_mapped_partitions(&partitions, &image_map, /*is_recovery=*/ false);
        assert_eq!(
            mapped,
            vec![
                v3::Partition { name: "part1".into(), path: "zbi_a".into(), condition: None },
                v3::Partition { name: "part2".into(), path: "vbmeta_a".into(), condition: None },
                v3::Partition { name: "part5".into(), path: "zbi_r".into(), condition: None },
                v3::Partition { name: "part6".into(), path: "vbmeta_r".into(), condition: None },
                v3::Partition { name: "part7".into(), path: "fvm_a".into(), condition: None },
            ]
        );
    }

    #[test]
    fn test_get_mapped_partitions_recovery() {
        let partitions = vec![
            Partition::ZBI { name: "part1".into(), slot: Slot::A },
            Partition::VBMeta { name: "part2".into(), slot: Slot::A },
            Partition::ZBI { name: "part3".into(), slot: Slot::B },
            Partition::VBMeta { name: "part4".into(), slot: Slot::B },
            Partition::ZBI { name: "part5".into(), slot: Slot::R },
            Partition::VBMeta { name: "part6".into(), slot: Slot::R },
            Partition::FVM { name: "part7".into() },
        ];
        let image_map: ImageMap = btreemap! {
            Slot::A => btreemap!{
                ImageType::ZBI => "zbi_a".into(),
                ImageType::VBMeta => "vbmeta_a".into(),
                ImageType::FVM => "fvm_a".into(),
            },
            Slot::B => btreemap!{
                ImageType::ZBI => "zbi_b".into(),
                ImageType::VBMeta => "vbmeta_b".into(),
                ImageType::FVM => "fvm_b".into(),
            },
            Slot::R => btreemap!{
                ImageType::ZBI => "zbi_r".into(),
                ImageType::VBMeta => "vbmeta_r".into(),
            },
        };
        let mapped = get_mapped_partitions(&partitions, &image_map, /*is_recovery=*/ true);
        assert_eq!(
            mapped,
            vec![
                v3::Partition { name: "part1".into(), path: "zbi_r".into(), condition: None },
                v3::Partition { name: "part2".into(), path: "vbmeta_r".into(), condition: None },
                v3::Partition { name: "part3".into(), path: "zbi_r".into(), condition: None },
                v3::Partition { name: "part4".into(), path: "vbmeta_r".into(), condition: None },
                v3::Partition { name: "part5".into(), path: "zbi_r".into(), condition: None },
                v3::Partition { name: "part6".into(), path: "vbmeta_r".into(), condition: None },
            ]
        );
    }
}
