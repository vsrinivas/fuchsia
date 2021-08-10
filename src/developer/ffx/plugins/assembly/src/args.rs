// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use std::path::PathBuf;

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
    VBMeta(VBMetaArgs),
    Image(ImageArgs),
    Extract(ExtractArgs),
}

/// vbmeta operations
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "vbmeta")]
pub struct VBMetaArgs {
    /// the vbmeta operation to perform
    #[argh(subcommand)]
    pub operation: VBMetaOperation,
}

#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand)]
pub enum VBMetaOperation {
    Sign(SignArgs),
}

/// create and sign a vbmeta image.
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "sign")]
pub struct SignArgs {
    /// the name of the image being signed
    #[argh(option)]
    pub name: String,

    /// the path to the image to sign
    #[argh(option)]
    pub image_path: PathBuf,

    /// the path to the PEM file containing the signing key to use.
    #[argh(option)]
    pub key: PathBuf,

    /// the path to the metadata file for the signing key.
    #[argh(option)]
    pub key_metadata: PathBuf,

    /// the file containing salt for the vbmeta signing operation.
    ///
    /// This is only to be used as part of testing the vbmeta signing operation,
    /// and must be a path to a file that contains a 64-char hex string of bytes.
    #[argh(option)]
    pub salt_file: Option<PathBuf>,

    /// descriptors for additional partitions to include, as paths to json files
    #[argh(option)]
    pub additional_descriptor: Vec<PathBuf>,

    /// the output file to write the vbmeta image to.
    #[argh(option)]
    pub output: PathBuf,
}

/// perform the assembly of images
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "image")]
pub struct ImageArgs {
    /// the configuration file that specifies the packages, binaries, and
    /// settings specific to the product being assembled.
    #[argh(option)]
    pub product: PathBuf,

    /// the configuration file that specifies the packages, binaries, and
    /// settings specific to the board being assembled.
    #[argh(option)]
    pub board: PathBuf,

    /// the directory to write assembled outputs to.
    #[argh(option)]
    pub outdir: PathBuf,

    /// the directory to write generated intermediate files to.
    #[argh(option)]
    pub gendir: Option<PathBuf>,

    /// run all assembly steps, even though they haven't yet been fully integrated.
    /// This is a temporary argument.
    #[argh(switch)]
    pub full: bool,
}

/// extract information from an image.
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "extract")]
pub struct ExtractArgs {
    /// the directory to write extracted artifacts to.
    #[argh(option)]
    pub outdir: PathBuf,

    /// the zircon boot image in ZBI format, usually named fuchsia.zbi.
    #[argh(option)]
    pub zbi: PathBuf,
}
