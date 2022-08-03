// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, std::path::PathBuf, std::str::FromStr};

/// Discover and access product bundle metadata and image data.
#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "product-bundle")]
pub struct ProductBundleCommand {
    #[argh(subcommand)]
    pub sub: SubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum SubCommand {
    List(ListCommand),
    Get(GetCommand),
    Create(CreateCommand),
}

/// Display a list of product bundle names.
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "list")]
pub struct ListCommand {
    /// do no network IO, use the locally cached version or fail.
    #[argh(switch)]
    pub cached: bool,
}

/// Retrieve image data.
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "get")]
pub struct GetCommand {
    /// do no network IO, use the locally cached version or fail.
    #[argh(switch)]
    pub cached: bool,

    /// get (and cache) data for specific product bundle.
    #[argh(positional)]
    pub product_bundle_name: Option<String>,

    /// download to directory (Experimental).
    #[argh(option)]
    pub out_dir: Option<PathBuf>,

    /// repositories will be named `NAME`. Defaults to the product bundle name.
    #[argh(option)]
    pub repository: Option<String>,
}

/// Type of PBM.
#[derive(Clone, Debug, PartialEq)]
pub enum ProductBundleType {
    EMU,
    FLASH,
}

#[derive(Clone, Debug, PartialEq)]
pub struct ProductBundleTypes {
    pub types: Vec<ProductBundleType>,
}

impl ProductBundleTypes {
    pub fn contains(&self, t: ProductBundleType) -> bool {
        self.types.contains(&t)
    }
}

impl FromStr for ProductBundleTypes {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let mut vec = Vec::new();
        let lower = s.to_ascii_lowercase();
        for s in lower.split(",") {
            match s {
                "emu" => vec.push(ProductBundleType::EMU),
                "flash" => vec.push(ProductBundleType::FLASH),
                _ => {
                    return Err(format!(
                        "'{}' is not a valid value: must be one of 'emu', 'flash'",
                        s
                    ))
                }
            };
        }
        Ok(Self { types: vec })
    }
}

/// Create product bundle manifest file.
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "create")]
pub struct CreateCommand {
    /// is this product_bundle.json for emulator or flash.
    #[argh(option, short = 't')]
    pub types: ProductBundleTypes,

    /// location of packages directory.
    #[argh(option, short = 'p')]
    pub packages: String,

    /// location of images directory.
    #[argh(option, short = 'i')]
    pub images: String,

    /// path to multiboot.bin file.
    #[argh(option, short = 'm', default = "String::from(\"\")")]
    pub multiboot_bin: String,

    /// device_spec name.
    #[argh(option, short = 'd', default = "String::from(\"\")")]
    pub device_name: String,

    /// path to build_info.json file.
    #[argh(option, short = 'b')]
    pub build_info: String,

    /// path to flash manifest file.
    #[argh(option, short = 'f', default = "String::from(\"\")")]
    pub flash_manifest: String,

    /// path to output directory.
    #[argh(option, short = 'o', default = "PathBuf::from(\".\")")]
    pub out: PathBuf,
}
