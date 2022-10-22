// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs, ffx_core::ffx_command, pbms::AuthFlowChoice, std::path::PathBuf,
    std::str::FromStr,
};

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
    Remove(RemoveCommand),
}

/// Display a list of product bundle names.
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "list")]
pub struct ListCommand {
    /// do no network IO, use the locally cached version or fail.
    #[argh(switch)]
    pub cached: bool,

    /// use specific auth flow for oauth2.
    #[argh(option, default = "AuthFlowChoice::Default")]
    pub auth: AuthFlowChoice,

    /// use an insecure oauth2 token flow (deprecated).
    #[argh(switch)]
    pub oob_auth: bool,
}

/// Retrieve image data.
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "get")]
pub struct GetCommand {
    /// do no network IO, use the locally cached version or fail.
    #[argh(switch)]
    pub cached: bool,

    /// force a download even if the bundle is already stored locally.
    #[argh(switch)]
    pub force: bool,

    /// use specific auth flow for oauth2.
    #[argh(option, default = "AuthFlowChoice::Default")]
    pub auth: AuthFlowChoice,

    /// use an insecure oauth2 token flow (deprecated).
    #[argh(switch)]
    pub oob_auth: bool,

    /// get (and cache) data for specific product bundle.
    #[argh(positional)]
    pub product_bundle_name: Option<String>,

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

/// Remove a product bundle from the product bundle cache.
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "remove")]
pub struct RemoveCommand {
    /// remove all product bundles instead of just one.
    #[argh(switch, short = 'a')]
    pub all: bool,

    /// remove product bundle(s) without interactive confirmation.
    #[argh(switch, short = 'f')]
    pub force: bool,

    /// the name of the product bundle to remove.
    #[argh(positional)]
    pub product_bundle_name: Option<String>,
}
