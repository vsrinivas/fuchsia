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

    /// the output file to write the salt value used to
    #[argh(option)]
    pub salt_outfile: Option<PathBuf>,
}

/// perform the assembly of images
#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "image")]
pub struct ImageArgs {
    /// the configuration file that specifies the packages, binaries, and
    /// settings used to assemble an image.
    #[argh(option)]
    pub config: PathBuf,
    /// the directory to write assembled outputs to.
    #[argh(option)]
    pub outdir: PathBuf,
    /// the directory to write generated intermediate files to.
    #[argh(option)]
    pub gendir: Option<PathBuf>,
}
