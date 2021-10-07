// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base_package::BasePackage;
use crate::config::{BoardConfig, ProductConfig};
use crate::util::pkg_manifest_from_path;

use anyhow::{Context, Result};
use assembly_update_package::UpdatePackageBuilder;
use fuchsia_pkg::PackagePath;
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

#[derive(Default)]
pub struct UpdatePackage {
    pub contents: BTreeMap<String, String>,
    pub path: PathBuf,
}

pub fn construct_update(
    outdir: impl AsRef<Path>,
    gendir: impl AsRef<Path>,
    product: &ProductConfig,
    board: &BoardConfig,
    zbi: impl AsRef<Path>,
    vbmeta: Option<impl AsRef<Path>>,
    base_package: Option<&BasePackage>,
) -> Result<UpdatePackage> {
    // Create the board name file.
    // TODO(fxbug.dev/76326): Create a better system for writing intermediate files.
    let board_name = gendir.as_ref().join("board");
    std::fs::write(&board_name, &board.board_name)?;

    // Add several files to the update package.
    let mut update_pkg_builder = UpdatePackageBuilder::new();
    if let Some(epoch_file) = &board.epoch_file {
        update_pkg_builder.add_file(epoch_file, "epoch.json")?;
    }
    if let Some(version_file) = &board.version_file {
        update_pkg_builder.add_file(version_file, "version")?;
    }
    update_pkg_builder.add_file(&board_name, "board")?;

    let zbi_destination = if board.zbi.signing_script.is_some() { "zbi.signed" } else { "zbi" };
    update_pkg_builder.add_file(zbi, zbi_destination)?;

    if let Some(vbmeta) = vbmeta {
        update_pkg_builder.add_file(vbmeta, "fuchsia.vbmeta")?;
    }

    if let Some(recovery_config) = &board.recovery {
        update_pkg_builder.add_file(&recovery_config.zbi, &recovery_config.name)?;

        // TODO(fxbug.dev/77997)
        // TODO(fxbug.dev/77535)
        // TODO(fxbug.dev/76371)
        //
        // Determine what to do with this case:
        //  - is it an error if fuchsia.vbmeta is present, but recovery.vbmeta isn't?
        //  - do we generate/sign our own recovery.vbmeta?
        //  - etc.
        if let Some(recovery_vbmeta) = &recovery_config.vbmeta {
            update_pkg_builder.add_file(recovery_vbmeta, "recovery.vbmeta")?;
        }
    }

    // Add the bootloaders.
    for bootloader in &board.bootloaders {
        update_pkg_builder.add_file(&bootloader.source, &bootloader.name)?;
    }

    // Add the packages that need to be updated.
    let mut add_packages_to_update = |packages: &Vec<PathBuf>| -> Result<()> {
        for package_path in packages {
            let manifest = pkg_manifest_from_path(package_path)?;
            update_pkg_builder.add_package_by_manifest(manifest)?;
        }
        Ok(())
    };
    add_packages_to_update(&product.base)?;
    add_packages_to_update(&product.cache)?;

    if let Some(base_package) = &base_package {
        // Add the base package merkle.
        update_pkg_builder.add_package(
            PackagePath::from_name_and_variant(
                board.base_package_name.parse().context("parse package name")?,
                "0".parse().context("parse package variant")?,
            ),
            base_package.merkle,
        )?;
    }

    // Build the update package and return its path.
    let update_package_path = outdir.as_ref().join("update.far");
    let update_contents = update_pkg_builder
        .build(outdir, gendir, &board.update_package_name, &update_package_path)
        .context("Failed to build the update package")?;
    Ok(UpdatePackage { contents: update_contents, path: update_package_path })
}

#[cfg(test)]
mod tests {
    use super::construct_update;

    use crate::base_package::BasePackage;
    use crate::config::{BoardConfig, ProductConfig};
    use fuchsia_archive::Reader;
    use fuchsia_hash::Hash;
    use std::collections::BTreeMap;
    use std::fs::File;
    use std::str::FromStr;
    use tempfile::tempdir;

    #[test]
    fn construct() {
        let dir = tempdir().unwrap();

        // Create fake product/board definitions.
        let product_config = ProductConfig::new("kernel", 0);
        let board_config = BoardConfig::new("board");

        // Create a fake zbi.
        let zbi_path = dir.path().join("fuchsia.zbi");
        std::fs::write(&zbi_path, "fake zbi").unwrap();

        // Create a fake vbmeta.
        let vbmeta_path = dir.path().join("fuchsia.vbmeta");
        std::fs::write(&vbmeta_path, "fake vbmeta").unwrap();

        // Create a fake base package.
        let base_path = dir.path().join("base.far");
        std::fs::write(&base_path, "fake base").unwrap();
        let base = BasePackage {
            merkle: Hash::from_str(
                "0000000000000000000000000000000000000000000000000000000000000000",
            )
            .unwrap(),
            contents: BTreeMap::default(),
            path: base_path,
        };

        // Construct the update package.
        let update_package = construct_update(
            dir.path(),
            dir.path(),
            &product_config,
            &board_config,
            &zbi_path,
            Some(vbmeta_path),
            Some(&base),
        )
        .unwrap();
        assert_eq!(update_package.path, dir.path().join("update.far"));

        // Read the update package, and assert the contents are correct.
        let update_package_file = File::open(update_package.path).unwrap();
        let mut far_reader = Reader::new(&update_package_file).unwrap();
        let contents = far_reader.read_file("meta/contents").unwrap();
        let contents = std::str::from_utf8(&contents).unwrap();
        let expected_contents = "\
            board=9c579992f6e9f8cbd4ba81af6e23b1d5741e280af60f795e9c2bbcc76c4b7065\n\
            fuchsia.vbmeta=4dfd304408a2608ac4a7a6d173d4d55eb0a8411c536c581746b7f705c4b00919\n\
            packages.json=3b42e8113041a6ae16f5996e55185324f6a9f986d93f7ee546ae857e10bd79f5\n\
            zbi=2162b78584bc362ffae4dddca33b9ccb55c38b725279f2f191b852f3c5558348\n\
        "
        .to_string();
        assert_eq!(contents, expected_contents);
    }

    #[test]
    fn construct_prime() {
        let dir = tempdir().unwrap();

        // Create fake product/board definitions.
        let product_config = ProductConfig::new("kernel", 0);
        let board_config = BoardConfig::new("board");

        // Create a fake zbi.
        let zbi_path = dir.path().join("fuchsia.zbi");
        std::fs::write(&zbi_path, "fake zbi").unwrap();

        // Create a fake vbmeta.
        let vbmeta_path = dir.path().join("fuchsia.vbmeta");
        std::fs::write(&vbmeta_path, "fake vbmeta").unwrap();

        // Create a fake base package.
        let base_path = dir.path().join("base.far");
        std::fs::write(&base_path, "fake base").unwrap();
        let base = BasePackage {
            merkle: Hash::from_str(
                "0000000000000000000000000000000000000000000000000000000000000000",
            )
            .unwrap(),
            contents: BTreeMap::default(),
            path: base_path,
        };

        // Construct the update package.
        let update_package = construct_update(
            dir.path(),
            dir.path(),
            &product_config,
            &board_config,
            &zbi_path,
            Some(vbmeta_path),
            Some(&base),
        )
        .unwrap();
        assert_eq!(update_package.path, dir.path().join("update.far"));

        // Read the update package, and assert the contents are correct.
        let update_package_file = File::open(update_package.path).unwrap();
        let mut far_reader = Reader::new(&update_package_file).unwrap();
        let contents = far_reader.read_file("meta/package").unwrap();
        let contents = std::str::from_utf8(&contents).unwrap();

        // The name remains "update" even for prime.
        let expected_contents = r#"{"name":"update","version":"0"}"#.to_string();
        assert_eq!(contents, expected_contents);
    }
}
