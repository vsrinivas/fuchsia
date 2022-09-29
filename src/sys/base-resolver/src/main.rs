// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_component_resolution as fresolution};

mod base_resolver;
mod pkg_cache_resolver;

#[fuchsia::main]
async fn main() -> anyhow::Result<()> {
    let args = std::env::args().collect::<Vec<_>>();
    let args = args.iter().map(|s| s.as_str()).collect::<Vec<_>>();
    match args.as_slice() {
        ["/pkg/bin/base_resolver", "base_resolver"] => base_resolver::main().await,
        ["/pkg/bin/base_resolver", "pkg_cache_resolver"] => pkg_cache_resolver::main().await,
        _ => {
            tracing::error!("invalid args {:?}", args);
            anyhow::bail!("invalid args {:?}", args)
        }
    }
}

#[derive(thiserror::Error, Debug)]
enum ResolverError {
    #[error("invalid component URL")]
    InvalidUrl(#[from] fuchsia_url::errors::ParseError),

    #[error("component URL with package hash not supported")]
    PackageHashNotSupported,

    #[error("the hostname refers to an unsupported repo")]
    UnsupportedRepo,

    #[error("component not found")]
    ComponentNotFound(#[source] mem_util::FileError),

    #[error("package not found")]
    PackageNotFound(#[source] fuchsia_fs::node::OpenError),

    #[error("couldn't parse component manifest")]
    ParsingManifest(#[source] fidl::Error),

    #[error("couldn't find config values")]
    ConfigValuesNotFound(#[source] mem_util::FileError),

    #[error("config source missing or invalid")]
    InvalidConfigSource,

    #[error("unsupported config source: {:?}", _0)]
    UnsupportedConfigSource(fdecl::ConfigValueSource),

    #[error("failed to read the manifest")]
    ReadManifest(#[source] mem_util::DataError),

    #[error("failed to create FIDL endpoints")]
    CreateEndpoints(#[source] fidl::Error),

    #[error("serve package directory")]
    ServePackageDirectory(#[source] package_directory::Error),

    #[error("failed to read the resolution context")]
    ReadingContext(#[source] anyhow::Error),

    #[error("failed to create the resolution context")]
    CreatingContext(#[source] anyhow::Error),

    #[error("subpackage name was not found in the package's subpackage list")]
    SubpackageNotFound(#[source] anyhow::Error),

    #[error("the subpackage hash was not found in the system base package index: {0}")]
    SubpackageNotInBase(#[source] anyhow::Error),

    #[error("missing context required to resolve relative url: {0}")]
    RelativeUrlMissingContext(String),

    #[error("converting a FIDL proxy into a FIDL client")]
    ConvertProxyToClient,

    #[error("internal error")]
    Internal,
}

impl From<&ResolverError> for fresolution::ResolverError {
    fn from(err: &ResolverError) -> fresolution::ResolverError {
        use {fresolution::ResolverError as ferror, ResolverError::*};
        match err {
            InvalidUrl(_) | PackageHashNotSupported => ferror::InvalidArgs,
            UnsupportedRepo => ferror::NotSupported,
            ComponentNotFound(_) => ferror::ManifestNotFound,
            PackageNotFound(_) => ferror::PackageNotFound,
            ConfigValuesNotFound(_) => ferror::ConfigValuesNotFound,
            ParsingManifest(_) | UnsupportedConfigSource(_) | InvalidConfigSource => {
                ferror::InvalidManifest
            }
            ReadManifest(_)
            | CreateEndpoints(_)
            | ServePackageDirectory(_)
            | ConvertProxyToClient => ferror::Io,
            CreatingContext(_) | ReadingContext(_) | RelativeUrlMissingContext(_) | Internal => {
                ferror::Internal
            }
            SubpackageNotInBase(_) | SubpackageNotFound(_) => ferror::PackageNotFound,
        }
    }
}
