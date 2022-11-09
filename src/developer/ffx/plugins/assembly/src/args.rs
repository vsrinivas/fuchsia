// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Result};
use argh::FromArgs;
use camino::Utf8PathBuf;
use ffx_core::ffx_command;
use std::str::FromStr;

use assembly_images_config::BlobFSLayout;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "assembly", description = "Assemble images")]
pub struct AssemblyCommand {
    /// the assembly operation to perform
    #[argh(subcommand)]
    pub op_class: OperationClass,
}

/// This is the set of top-level operations within the `ffx assembly` plugin
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand)]
pub enum OperationClass {
    CreateSystem(CreateSystemArgs),
    CreateUpdate(CreateUpdateArgs),
    CreateFlashManifest(CreateFlashManifestArgs),
    Product(ProductArgs),
    SizeCheck(SizeCheckArgs),
}

/// create the system images.
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "create-system")]
pub struct CreateSystemArgs {
    /// the configuration file that specifies the packages, binaries, and
    /// settings specific to the product being assembled.
    #[argh(option)]
    pub image_assembly_config: Utf8PathBuf,

    /// the configuration file that specifies which images to generate and how.
    #[argh(option)]
    pub images: Utf8PathBuf,

    /// the directory to write assembled outputs to.
    #[argh(option)]
    pub outdir: Utf8PathBuf,

    /// the directory to write generated intermediate files to.
    #[argh(option)]
    pub gendir: Option<Utf8PathBuf>,

    /// name to give the Base Package. This is useful if you must publish multiple
    /// base packages to the same TUF repository.
    #[argh(option)]
    pub base_package_name: Option<String>,

    /// mode indicating where to place packages.
    #[argh(option, default = "default_package_mode()")]
    pub mode: PackageMode,
}

/// Mode indicating where to place packages.
#[derive(Debug, PartialEq)]
pub enum PackageMode {
    /// Put packages in a FVM.
    Fvm,

    /// Put packages in a FVM as a ramdisk in the ZBI.
    FvmInZbi,

    /// Put packages into BootFS.
    BootFS,
}

impl FromStr for PackageMode {
    type Err = anyhow::Error;
    fn from_str(s: &str) -> Result<Self> {
        match s {
            "fvm" => Ok(PackageMode::Fvm),
            "fvm-in-zbi" => Ok(PackageMode::FvmInZbi),
            "bootfs" => Ok(PackageMode::BootFS),
            _ => Err(anyhow!("invalid package mode: {}", s)),
        }
    }
}

fn default_package_mode() -> PackageMode {
    PackageMode::Fvm
}

/// construct an UpdatePackage using images and package.
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "create-update")]
pub struct CreateUpdateArgs {
    /// path to a packages manifest, which specifies what packages to update.
    #[argh(option)]
    pub packages: Option<Utf8PathBuf>,

    /// path to a partitions config, which specifies where in the partition
    /// table the images are put.
    #[argh(option)]
    pub partitions: Utf8PathBuf,

    /// path to an images manifest, which specifies images to put in slot A.
    #[argh(option)]
    pub system_a: Option<Utf8PathBuf>,

    /// path to an images manifest, which specifies images to put in slot R.
    #[argh(option)]
    pub system_r: Option<Utf8PathBuf>,

    /// name of the board.
    /// Fuchsia will reject an Update Package with a different board name.
    #[argh(option)]
    pub board_name: String,

    /// file containing the version of the Fuchsia system.
    #[argh(option)]
    pub version_file: Utf8PathBuf,

    /// backstop OTA version.
    /// Fuchsia will reject updates with a lower epoch.
    #[argh(option)]
    pub epoch: u64,

    /// name to give the Update Package.
    /// This is currently only used by OTA tests to allow publishing multiple
    /// update packages to the same amber repository without naming collisions.
    #[argh(option)]
    pub update_package_name: Option<String>,

    /// directory to write the UpdatePackage.
    #[argh(option)]
    pub outdir: Utf8PathBuf,

    /// directory to write intermediate files.
    #[argh(option)]
    pub gendir: Option<Utf8PathBuf>,
}

/// construct a flash manifest.
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "create-flash-manifest")]
pub struct CreateFlashManifestArgs {
    /// path to a partitions config, which specifies where in the partition
    /// table the images are put.
    #[argh(option)]
    pub partitions: Utf8PathBuf,

    /// path to an images manifest, which specifies images to put in slot A.
    #[argh(option)]
    pub system_a: Option<Utf8PathBuf>,

    /// path to an images manifest, which specifies images to put in slot B.
    #[argh(option)]
    pub system_b: Option<Utf8PathBuf>,

    /// path to an images manifest, which specifies images to put in slot R.
    #[argh(option)]
    pub system_r: Option<Utf8PathBuf>,

    /// directory to write the UpdatePackage.
    #[argh(option)]
    pub outdir: Utf8PathBuf,
}

/// Perform size checks (on packages or product based on the sub-command).
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "size-check")]
pub struct SizeCheckArgs {
    #[argh(subcommand)]
    pub op_class: SizeCheckOperationClass,
}

/// The set of operations available under `ffx assembly size-check`.
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand)]
pub enum SizeCheckOperationClass {
    /// Check that the set of all blobs included in the product fit in the blobfs capacity.
    Product(ProductSizeCheckArgs),
    /// Check that package sets are not over their allocated budgets.
    Package(PackageSizeCheckArgs),
}

/// Measure package sizes and verify they fit in the specified budgets.
/// Exit status is 2 when one or more budgets are exceeded, and 1 when
/// a failure prevented the budget verification to happen.
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "package")]
pub struct PackageSizeCheckArgs {
    /// path to a JSON file containing the list of size budgets.
    /// Each size budget has a `name`, a `size` which is the maximum
    /// number of bytes, and `packages` a list of path to manifest files.
    #[argh(option)]
    pub budgets: Utf8PathBuf,
    /// path to a `blobs.json` file. It provides the size of each blob
    /// composing the package on device.
    #[argh(option)]
    pub blob_sizes: Vec<Utf8PathBuf>,
    /// the layout of blobs in blobfs.
    #[argh(option, default = "default_blobfs_layout()")]
    pub blobfs_layout: BlobFSLayout,
    /// path where to write the verification report, in JSON format.
    #[argh(option)]
    pub gerrit_output: Option<Utf8PathBuf>,
    /// show the storage consumption of each component broken down by package
    /// regardless of whether the component exceeded its budget.
    #[argh(switch, short = 'v')]
    pub verbose: bool,
    /// path where to write the verbose JSON output.
    #[argh(option)]
    pub verbose_json_output: Option<Utf8PathBuf>,
}

/// (Not implemented yet) Check that the set of all blobs included in the product
/// fit in the blobfs capacity.
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "product")]
pub struct ProductSizeCheckArgs {
    /// path to assembly_manifest.json.
    #[argh(option)]
    pub assembly_manifest: Utf8PathBuf,
    /// path to the bast assembly_manifest.json which will be used to compare with the current
    /// assembly_manifest.json to produce a diff.
    #[argh(option)]
    pub base_assembly_manifest: Option<Utf8PathBuf>,
    /// whether to show the verbose output.
    #[argh(switch, short = 'v')]
    pub verbose: bool,
    /// path to the directory where HTML visualization should be stored.
    #[argh(option)]
    pub visualization_dir: Option<Utf8PathBuf>,
    /// path where to write the gerrit size report.
    #[argh(option)]
    pub gerrit_output: Option<Utf8PathBuf>,
    /// path where to write the size breakdown.
    #[argh(option)]
    pub size_breakdown_output: Option<Utf8PathBuf>,
    /// maximum amount that the size of blobfs can increase in one CL.
    /// This value is propagated to the gerrit size report.
    #[argh(option)]
    pub blobfs_creep_budget: Option<u64>,
}

fn default_blobfs_layout() -> BlobFSLayout {
    BlobFSLayout::Compact
}

/// Arguments for performing a high-level product assembly operation.
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "product")]
pub struct ProductArgs {
    /// the configuration file that describes the product assembly to perform.
    #[argh(option)]
    pub product: Utf8PathBuf,

    /// the file containing information about the board that the product is
    /// being assembled to run on.
    #[argh(option)]
    pub board_info: Option<Utf8PathBuf>,

    /// the directory to write assembled outputs to.
    #[argh(option)]
    pub outdir: Utf8PathBuf,

    /// the directory to write generated intermediate files to.
    #[argh(option)]
    pub gendir: Option<Utf8PathBuf>,

    /// the directory in which to find the platform assembly input bundles
    #[argh(option)]
    pub input_bundles_dir: Utf8PathBuf,

    /// the path to the legacy assembly input bundle directory
    #[argh(option)]
    pub legacy_bundle: Utf8PathBuf,

    /// a file containing a ProductPackageConfig with additional packages
    /// to include which are not in the assembly input bundle
    #[argh(option)]
    pub additional_packages_path: Option<Utf8PathBuf>,
}
