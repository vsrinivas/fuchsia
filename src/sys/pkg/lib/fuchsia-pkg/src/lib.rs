// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
#[macro_use]
mod test;

mod build;
mod creation_manifest;
mod errors;
mod meta_contents;
mod meta_package;
mod package_manifest;
mod path;

pub use crate::build::build;
pub use crate::creation_manifest::CreationManifest;
pub use crate::errors::{BuildError, CreationManifestError, MetaPackageError};
pub use crate::meta_contents::MetaContents;
pub use crate::meta_package::MetaPackage;
pub use crate::package_manifest::PackageManifest;
pub use crate::path::{check_package_name, check_package_variant, check_resource_path};
