// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, camino::Utf8PathBuf, std::path::PathBuf};

/// Builds a package.
#[derive(FromArgs, PartialEq, Debug, Default)]
#[argh(subcommand, name = "build")]
pub struct PackageBuildCommand {
    #[argh(
        option,
        short = 'o',
        default = "Utf8PathBuf::from(\"./out\")",
        description = "directory to save package artifacts"
    )]
    pub out: Utf8PathBuf,

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

    #[argh(option, description = "path to the subpackages manifest file (experimental)")]
    pub subpackages_manifest_path: Option<Utf8PathBuf>,

    #[argh(positional, description = "path to the creation manifest file")]
    pub creation_manifest_path: Utf8PathBuf,
}

/// Create a repository.
#[derive(FromArgs, PartialEq, Debug, Default)]
#[argh(subcommand, name = "create")]
pub struct RepoCreateCommand {
    #[argh(
        switch,
        description = "set repository version based on the current time rather than monotonically increasing version"
    )]
    pub time_versioning: bool,

    #[argh(option, description = "path to the repository keys directory")]
    pub keys: PathBuf,

    #[argh(positional, description = "path to the repository directory")]
    pub repo_path: Utf8PathBuf,
}

/// Publish packages.
#[derive(FromArgs, PartialEq, Debug, Default)]
#[argh(subcommand, name = "publish")]
pub struct RepoPublishCommand {
    #[argh(option, description = "path to the repository keys directory")]
    pub keys: Option<Utf8PathBuf>,

    #[argh(option, long = "package", description = "path to a package manifest")]
    pub package_manifests: Vec<Utf8PathBuf>,

    #[argh(option, long = "package-list", description = "path to a packages list manifest")]
    pub package_list_manifests: Vec<Utf8PathBuf>,

    #[argh(
        switch,
        description = "set repository version based on time rather than monotonically increasing version"
    )]
    pub time_versioning: bool,

    #[argh(switch, description = "clean the repository so only new publications remain")]
    pub clean: bool,

    #[argh(option, description = "produce a depfile file")]
    pub depfile: Option<Utf8PathBuf>,

    #[argh(positional, description = "path to the repository directory")]
    pub repo_path: Utf8PathBuf,
}
