// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};
use assembly_images_manifest::{Image, ImagesManifest};
use assembly_partitions_config::PartitionsConfig;
use assembly_update_packages_manifest::UpdatePackagesManifest;
use assembly_util::PathToStringExt;
use epoch::EpochFile;
use fuchsia_pkg::PackageBuilder;
use std::path::{Path, PathBuf};

/// A builder that constructs update packages.
pub struct UpdatePackageBuilder {
    /// Name of the UpdatePackage.
    /// This is typically only modified for OTA tests so that multiple UpdatePackages can be
    /// published to the same repository.
    name: String,

    /// Mapping of physical partitions to images.
    partitions: PartitionsConfig,

    /// Name of the board.
    /// Fuchsia confirms the board name matches before applying an update.
    board_name: String,

    /// Version of the update.
    version_file: PathBuf,

    /// The epoch of the system.
    /// Fuchsia confirms that the epoch changes in increasing order before applying an update.
    epoch: EpochFile,

    /// Images to update for a particular slot, such as the ZBI or VBMeta for SlotA.
    /// Currently, the UpdatePackage does not support both A and B slots.
    slot_primary: Option<Slot>,
    slot_recovery: Option<Slot>,

    /// Manifest of packages to include in the update.
    packages: UpdatePackagesManifest,

    /// Directory to write outputs.
    outdir: PathBuf,

    /// Directory to write intermediate files.
    gendir: PathBuf,
}

/// A set of images to be updated in a particular slot.
pub enum Slot {
    /// A or B slots.
    Primary(ImagesManifest),

    /// R slot.
    Recovery(ImagesManifest),
}

impl Slot {
    /// Get the image manifest.
    fn manifest(&self) -> &ImagesManifest {
        match self {
            Slot::Primary(m) => m,
            Slot::Recovery(m) => m,
        }
    }
}

/// A mapping between an image source path on host to the destination in an UpdatePackage.
struct ImageMapping {
    source: PathBuf,
    destination: String,
}

impl ImageMapping {
    /// Create a new Image Mapping from |source | to |destination|.
    fn new(source: impl AsRef<Path>, destination: impl AsRef<str>) -> Self {
        Self {
            source: source.as_ref().to_path_buf(),
            destination: destination.as_ref().to_string(),
        }
    }

    /// Create an ImageMapping from the |image| and |slot|.
    fn try_from(image: &Image, slot: &Slot) -> Result<Self> {
        match slot {
            Slot::Primary(_) => match image {
                Image::ZBI { path: _, signed: true } => {
                    Ok(ImageMapping::new(image.source(), "zbi.signed"))
                }
                Image::ZBI { path: _, signed: false } => {
                    Ok(ImageMapping::new(image.source(), "zbi"))
                }
                Image::VBMeta(_) => Ok(ImageMapping::new(image.source(), "fuchsia.vbmeta")),
                _ => Err(anyhow!("Invalid primary image mapping")),
            },
            Slot::Recovery(_) => match image {
                Image::ZBI { path: _, signed: _ } => {
                    Ok(ImageMapping::new(image.source(), "recovery"))
                }
                Image::VBMeta(_) => Ok(ImageMapping::new(image.source(), "recovery.vbmeta")),
                _ => Err(anyhow!("Invalid recovery image mapping")),
            },
        }
    }
}

impl UpdatePackageBuilder {
    /// Construct a new UpdatePackageBuilder with the minimal requirements for an UpdatePackage.
    pub fn new(
        partitions: PartitionsConfig,
        board_name: impl AsRef<str>,
        version_file: impl AsRef<Path>,
        epoch: EpochFile,
        outdir: impl AsRef<Path>,
    ) -> Self {
        Self {
            name: "update".into(),
            partitions,
            board_name: board_name.as_ref().into(),
            version_file: version_file.as_ref().to_path_buf(),
            epoch,
            slot_primary: None,
            slot_recovery: None,
            packages: UpdatePackagesManifest::default(),
            outdir: outdir.as_ref().to_path_buf(),
            gendir: outdir.as_ref().to_path_buf(),
        }
    }

    /// Set the name of the UpdatePackage.
    pub fn set_name(&mut self, name: impl AsRef<str>) {
        self.name = name.as_ref().to_string();
    }

    /// Set the directory for writing intermediate files.
    pub fn set_gendir(&mut self, gendir: impl AsRef<Path>) {
        self.gendir = gendir.as_ref().to_path_buf();
    }

    /// Update the images in |slot|.
    pub fn add_slot_images(&mut self, slot: Slot) {
        match slot {
            Slot::Primary(_) => self.slot_primary = Some(slot),
            Slot::Recovery(_) => self.slot_recovery = Some(slot),
        }
    }

    /// Add |packages| to the update.
    pub fn add_packages(&mut self, packages: UpdatePackagesManifest) {
        self.packages.append(packages);
    }

    /// Add the ZBI and VBMeta from the |slot| to the |map|.
    fn add_images_to_builder(slot: &Slot, builder: &mut PackageBuilder) -> Result<()> {
        let mappings: Vec<ImageMapping> = slot
            .manifest()
            .images
            .iter()
            .filter_map(|i| ImageMapping::try_from(i, slot).ok())
            .collect();
        for ImageMapping { source, destination } in mappings {
            builder.add_file_as_blob(destination, source.path_to_string()?)?;
        }
        Ok(())
    }

    /// Build the update package.
    pub fn build(self) -> Result<()> {
        use serde_json::to_string;

        // All update packages need to be named 'update' to be accepted by the
        // `system-updater`.
        let mut builder = PackageBuilder::new("update");
        // However, they can have different published names.  And the name here
        // is the name to publish it under (and to include in the generated
        // package manifest).
        builder.published_name(self.name);

        builder.add_contents_as_blob("packages.json", to_string(&self.packages)?, &self.gendir)?;
        builder.add_contents_as_blob("epoch.json", to_string(&self.epoch)?, &self.gendir)?;
        builder.add_contents_as_blob("board", &self.board_name, &self.gendir)?;

        builder.add_file_as_blob("version", self.version_file.path_to_string()?)?;

        // Add the images.
        let slots = vec![&self.slot_primary, &self.slot_recovery];
        for slot in slots.iter().filter_map(|s| s.as_ref()) {
            Self::add_images_to_builder(slot, &mut builder)?;
        }

        // Add the bootloaders.
        for bootloader in &self.partitions.bootloader_partitions {
            let destination = match bootloader.partition_type.as_str() {
                "" => "firmware".to_string(),
                t => format!("firmware_{}", t),
            };
            builder.add_file_as_blob(destination, bootloader.image.path_to_string()?)?;
        }

        let update_package_path = self.outdir.join("update.far");
        builder.manifest_path(self.outdir.join("update_package_manifest.json"));
        builder
            .build(&self.gendir, &update_package_path)
            .context("Failed to build the update package")?;

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assembly_partitions_config::Slot as PartitionSlot;
    use assembly_partitions_config::{BootloaderPartition, Partition, PartitionsConfig};
    use assembly_update_packages_manifest::UpdatePackagesManifest;

    use assembly_images_manifest::Image;
    use fuchsia_archive::Reader;
    use fuchsia_hash::{Hash, HASH_SIZE};
    use fuchsia_pkg::{PackageManifest, PackagePath};
    use fuchsia_url::pkg_url::PkgUrl;
    use std::fs::File;
    use std::io::{BufReader, Write};
    use std::str::FromStr;
    use tempfile::{tempdir, NamedTempFile};

    #[test]
    fn build() {
        let outdir = tempdir().unwrap();

        let fake_bootloader = NamedTempFile::new().unwrap();
        let partitions_config = PartitionsConfig {
            bootloader_partitions: vec![BootloaderPartition {
                partition_type: "tpl".into(),
                name: Some("firmware_tpl".into()),
                image: fake_bootloader.path().to_path_buf(),
            }],
            partitions: vec![Partition::ZBI { name: "zircon_a".into(), slot: PartitionSlot::A }],
        };
        let epoch = EpochFile::Version1 { epoch: 0 };
        let mut fake_version = NamedTempFile::new().unwrap();
        writeln!(fake_version, "1.2.3.4").unwrap();
        let mut builder = UpdatePackageBuilder::new(
            partitions_config,
            "board",
            fake_version.path().to_path_buf(),
            epoch.clone(),
            &outdir.path(),
        );

        // Add a ZBI to the update.
        let fake_zbi = NamedTempFile::new().unwrap();
        builder.add_slot_images(Slot::Primary(ImagesManifest {
            images: vec![Image::ZBI { path: fake_zbi.path().to_path_buf(), signed: true }],
        }));
        builder.build().unwrap();

        let file = File::open(outdir.path().join("packages.json")).unwrap();
        let reader = BufReader::new(file);
        let p: UpdatePackagesManifest = serde_json::from_reader(reader).unwrap();
        assert_eq!(UpdatePackagesManifest::default(), p);

        let file = File::open(outdir.path().join("epoch.json")).unwrap();
        let reader = BufReader::new(file);
        let e: EpochFile = serde_json::from_reader(reader).unwrap();
        assert_eq!(epoch, e);

        let b = std::fs::read_to_string(outdir.path().join("board")).unwrap();
        assert_eq!("board", b);

        // Read the output and ensure it contains the right files (and their hashes).
        let far_path = outdir.path().join("update.far");
        let mut far_reader = Reader::new(File::open(&far_path).unwrap()).unwrap();
        let package = far_reader.read_file("meta/package").unwrap();
        assert_eq!(package, br#"{"name":"update","version":"0"}"#);
        let contents = far_reader.read_file("meta/contents").unwrap();
        let contents = std::str::from_utf8(&contents).unwrap();
        let expected_contents = "\
            board=9c579992f6e9f8cbd4ba81af6e23b1d5741e280af60f795e9c2bbcc76c4b7065\n\
            epoch.json=0362de83c084397826800778a1cf927280a5d5388cb1f828d77f74108726ad69\n\
            firmware_tpl=15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b\n\
            packages.json=85a3911ff39c118ee1a4be5f7a117f58a5928a559f456b6874440a7fb8c47a9a\n\
            version=d2ff44655653e2cbbecaf89dbf33a8daa8867e41dade2c6b4f127c3f0450c96b\n\
            zbi.signed=15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b\n\
        "
        .to_string();
        assert_eq!(expected_contents, contents);
    }

    #[test]
    fn name() {
        let outdir = tempdir().unwrap();

        let mut fake_version = NamedTempFile::new().unwrap();
        writeln!(fake_version, "1.2.3.4").unwrap();
        let mut builder = UpdatePackageBuilder::new(
            PartitionsConfig::default(),
            "board",
            fake_version.path().to_path_buf(),
            EpochFile::Version1 { epoch: 0 },
            &outdir.path(),
        );
        builder.set_name("update_2");
        assert!(builder.build().is_ok());

        // Read the package manifest and ensure it contains the updated name.
        let manifest_path = outdir.path().join("update_package_manifest.json");
        let manifest_file = File::open(manifest_path).unwrap();
        let manifest: PackageManifest = serde_json::from_reader(manifest_file).unwrap();
        assert_eq!("update_2", manifest.name().as_ref());
    }

    #[test]
    fn packages() {
        let outdir = tempdir().unwrap();

        let mut fake_version = NamedTempFile::new().unwrap();
        writeln!(fake_version, "1.2.3.4").unwrap();
        let mut builder = UpdatePackageBuilder::new(
            PartitionsConfig::default(),
            "board",
            fake_version.path().to_path_buf(),
            EpochFile::Version1 { epoch: 0 },
            &outdir.path(),
        );

        let hash = Hash::from([0u8; HASH_SIZE]);
        let mut list1 = UpdatePackagesManifest::default();
        list1.add(PackagePath::from_str("one/0").unwrap(), hash.clone()).unwrap();
        list1.add(PackagePath::from_str("two/0").unwrap(), hash.clone()).unwrap();
        builder.add_packages(list1);

        let mut list2 = UpdatePackagesManifest::default();
        list2.add(PackagePath::from_str("three/0").unwrap(), hash.clone()).unwrap();
        list2.add(PackagePath::from_str("four/0").unwrap(), hash.clone()).unwrap();
        builder.add_packages(list2);

        assert!(builder.build().is_ok());

        // Read the package list and ensure it contains the correct contents.
        let package_list_path = outdir.path().join("packages.json");
        let package_list_file = File::open(package_list_path).unwrap();
        let list3: UpdatePackagesManifest = serde_json::from_reader(package_list_file).unwrap();
        let UpdatePackagesManifest::V1(pkg_urls) = list3;
        let pkg1 =
            PkgUrl::new_package("fuchsia.com".into(), "/one/0".into(), Some(hash.clone())).unwrap();
        println!("pkg_urls={:?}", &pkg_urls);
        println!("pkg={:?}", pkg1);
        assert!(pkg_urls.contains(&pkg1));
    }
}
