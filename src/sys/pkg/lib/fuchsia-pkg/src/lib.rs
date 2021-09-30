// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
pub mod test;

mod build;
mod creation_manifest;
mod errors;
mod meta_contents;
mod meta_package;
mod package;
mod package_directory;
mod package_manifest;
mod path;

pub use crate::{
    build::{build, build_with_file_system, FileSystem},
    creation_manifest::CreationManifest,
    errors::{
        BuildError, CreationManifestError, MetaContentsError, MetaPackageError,
        PackageManifestError, PackageNameError, PackagePathError, PackageVariantError,
        ParsePackagePathError,
    },
    meta_contents::MetaContents,
    meta_package::MetaPackage,
    package::{BlobEntry, Package, PackageBuilder},
    package_directory::{LoadMetaContentsError, OpenRights, PackageDirectory, ReadHashError},
    package_manifest::PackageManifest,
    path::{
        check_package_name, check_package_variant, check_resource_path, PackageName, PackagePath,
    },
};
