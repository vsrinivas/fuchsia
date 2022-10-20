// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::api::PackageResolver as PackageResolverApi;
use super::api::PackageResolverUrl;
use super::hash::Hash;
use std::iter;

/// TODO(fxbug.dev/111249): Implement production package resolver API.
#[derive(Default)]
pub(crate) struct PackageResolver;

impl PackageResolverApi for PackageResolver {
    type Hash = Hash;

    fn resolve(&self, _url: PackageResolverUrl) -> Option<Self::Hash> {
        None
    }

    fn aliases(&self, _hash: Self::Hash) -> Box<dyn Iterator<Item = PackageResolverUrl>> {
        Box::new(iter::empty())
    }
}
