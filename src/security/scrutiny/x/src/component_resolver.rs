// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::api::ComponentResolver as ComponentResolverApi;
use super::api::ComponentResolverUrl;
use super::hash::Hash;
use std::iter;

/// TODO(fxbug.dev/111250): Implement production component resolver API.
#[derive(Default)]
pub(crate) struct ComponentResolver;

impl ComponentResolverApi for ComponentResolver {
    type Hash = Hash;

    fn resolve(&self, _url: ComponentResolverUrl) -> Option<Self::Hash> {
        None
    }

    fn aliases(&self, _hash: Self::Hash) -> Box<dyn Iterator<Item = ComponentResolverUrl>> {
        Box::new(iter::empty())
    }
}
