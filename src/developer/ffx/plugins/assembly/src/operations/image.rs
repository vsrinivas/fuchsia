// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod config;

use anyhow::Result;
use assembly_base_package::BasePackageBuilder;
use config::Config;
use ffx_assembly_args::ImageArgs;
use ffx_core::ffx_bail;
use fuchsia_pkg::PackageManifest;
use std::fs::File;
use std::io::BufReader;
use std::path::PathBuf;

pub fn assemble(args: ImageArgs) -> Result<()> {
    let config = read_config(&args.config)?;
    let _base_package = construct_base_package(&args.gendir, &config)?;
    Ok(())
}

fn read_config(config_path: &String) -> Result<Config> {
    let mut config = File::open(config_path)?;
    let config = Config::from_reader(&mut config)
        .or_else(|e| ffx_bail!("Failed to read the image config: {}", e))?;
    println!("Config indicated version: {}", config.version);
    Ok(config)
}

fn construct_base_package(gendir: &PathBuf, config: &Config) -> Result<File> {
    let mut base_pkg_builder = BasePackageBuilder::default();
    for pkg_manifest_path in &config.extra_packages_for_base_package {
        let pkg_manifest = pkg_manifest_from_path(pkg_manifest_path);
        base_pkg_builder.add_files_from_package(pkg_manifest);
    }
    for pkg_manifest_path in &config.base_packages {
        let pkg_manifest = pkg_manifest_from_path(pkg_manifest_path);
        base_pkg_builder.add_base_package(pkg_manifest);
    }
    for pkg_manifest_path in &config.cache_packages {
        let pkg_manifest = pkg_manifest_from_path(pkg_manifest_path);
        base_pkg_builder.add_cache_package(pkg_manifest);
    }
    let mut base_package = File::create("base.far")
        .or_else(|e| ffx_bail!("Failed to create the base package file: {}", e))?;
    base_pkg_builder
        .build(gendir, &mut base_package)
        .or_else(|e| ffx_bail!("Failed to build the base package: {}", e))?;
    println!("Base package: base.far");
    Ok(base_package)
}

fn pkg_manifest_from_path(path: &String) -> PackageManifest {
    let manifest_file = std::fs::File::open(path).unwrap();
    let pkg_manifest_reader = BufReader::new(manifest_file);
    serde_json::from_reader(pkg_manifest_reader).unwrap()
}
