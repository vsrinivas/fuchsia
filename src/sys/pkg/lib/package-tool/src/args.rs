// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, std::path::PathBuf};

/// Builds a package.
#[derive(FromArgs, PartialEq, Debug, Default)]
#[argh(subcommand, name = "build")]
pub struct BuildCommand {
    #[argh(
        option,
        short = 'o',
        default = "PathBuf::from(\"./out\")",
        description = "directory to save package artifacts"
    )]
    pub out: PathBuf,

    #[argh(option, description = "package API level")]
    pub api_level: Option<u64>,

    #[argh(option, description = "package ABI revision")]
    pub abi_revision: Option<u64>,

    #[argh(option, description = "name of the package")]
    pub published_name: Option<String>,

    #[argh(option, description = "repository of the package")]
    pub repository: Option<String>,

    #[argh(switch, description = "produce a depfile file")]
    pub depfile: bool,

    #[argh(switch, description = "produce a meta.far.merkle file")]
    pub meta_far_merkle: bool,

    #[argh(switch, description = "produce a blobs.json file")]
    pub blobs_json: bool,

    #[argh(switch, description = "produce a blobs.manifest file")]
    pub blobs_manifest: bool,

    #[argh(positional, description = "path to the creation manifest file")]
    pub creation_manifest_path: PathBuf,
}
