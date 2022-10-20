// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::api::MetaContents as MetaContentsApi;
use super::api::MetaPackage as MetaPackageApi;
use super::api::Package as PackageApi;
use super::blob::Blob;
use super::component::Component;
use super::hash::Hash;
use std::iter;

/// TODO(fxbug.dev/111249): Implement production package API.
#[derive(Default)]
pub(crate) struct Package;

impl PackageApi for Package {
    type Hash = Hash;
    type MetaPackage = MetaPackage;
    type MetaContents = MetaContents;
    type Blob = Blob;
    type PackagePath = &'static str;
    type Component = Component;

    fn hash(&self) -> Self::Hash {
        Hash::default()
    }

    fn meta_package(&self) -> Self::MetaPackage {
        MetaPackage::default()
    }

    fn meta_contents(&self) -> Self::MetaContents {
        MetaContents::default()
    }

    fn content_blobs(&self) -> Box<dyn Iterator<Item = (Self::PackagePath, Self::Blob)>> {
        Box::new(iter::empty())
    }

    fn meta_blobs(&self) -> Box<dyn Iterator<Item = (Self::PackagePath, Self::Blob)>> {
        Box::new(iter::empty())
    }

    fn components(&self) -> Box<dyn Iterator<Item = (Self::PackagePath, Self::Component)>> {
        Box::new(iter::empty())
    }
}

/// TODO(fxbug.dev/111249): Implement for production package API.
#[derive(Default)]
pub(crate) struct MetaPackage;

impl MetaPackageApi for MetaPackage {}

/// TODO(fxbug.dev/111249): Implement for production package API.
#[derive(Default)]
pub(crate) struct MetaContents;

impl MetaContentsApi for MetaContents {}
