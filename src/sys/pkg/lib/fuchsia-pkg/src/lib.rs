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
mod meta_subpackages;
mod package;
mod package_builder;
pub mod package_directory;
mod package_manifest;
mod package_manifest_list;
mod path;
mod path_to_string;
mod subpackages_manifest;

pub use {
    crate::{
        creation_manifest::CreationManifest,
        errors::{
            BuildError, CreationManifestError, MetaContentsError, MetaPackageError,
            MetaSubpackagesError, PackageManifestError, ParsePackagePathError,
        },
        meta_contents::MetaContents,
        meta_package::MetaPackage,
        meta_subpackages::transitional,
        meta_subpackages::MetaSubpackages,
        package::{BlobEntry, Package},
        package_builder::PackageBuilder,
        package_directory::{
            LoadAbiRevisionError, LoadMetaContentsError, OpenRights, PackageDirectory,
            ReadHashError,
        },
        package_manifest::{BlobInfo, PackageManifest, PackageManifestBuilder, RelativeTo},
        package_manifest_list::PackageManifestList,
        path::{PackageName, PackagePath, PackageVariant},
        subpackages_manifest::{
            SubpackagesManifest, SubpackagesManifestEntry, SubpackagesManifestEntryKind,
        },
    },
    fuchsia_url::errors::PackagePathSegmentError,
    path_to_string::PathToStringExt,
};
