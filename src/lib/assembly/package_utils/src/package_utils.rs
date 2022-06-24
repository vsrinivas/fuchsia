// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use assembly_util::{impl_path_type_marker, PathTypeMarker, TypedPathBuf};
use serde::{Deserialize, Serialize};

/// PackageIdentity is an opaque type that allows for the string that's used as
/// a package's identity to be evolved over time, compared with other instances,
/// and used as a key in maps / sets.
#[derive(Debug, Clone, PartialEq, PartialOrd, Serialize, Deserialize)]
struct PackageIdentity(String);

impl std::str::FromStr for PackageIdentity {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Ok(Self(s.to_owned()))
    }
}

/// The marker trait for paths within a package
pub struct InternalPathMarker {}
impl_path_type_marker!(InternalPathMarker);

/// The semantic type for paths within a package
pub type PackageInternalPathBuf = TypedPathBuf<InternalPathMarker>;

/// The marker trait for the source path when that's ambiguous (like in a list
/// of source to destination paths)
pub struct SourcePathMarker {}
impl_path_type_marker!(SourcePathMarker);

/// The semantic type for paths that are the path to the source of a file to use
/// in some context.  Such as the source file for a blob in a package.
pub type SourcePathBuf = TypedPathBuf<SourcePathMarker>;

/// The marker trait for paths to a PackageManifest
pub struct PackageManifestPathMarker {}
impl_path_type_marker!(PackageManifestPathMarker);

/// The semantic type for paths that are the path to a package manifest.
pub type PackageManifestPathBuf = TypedPathBuf<PackageManifestPathMarker>;
