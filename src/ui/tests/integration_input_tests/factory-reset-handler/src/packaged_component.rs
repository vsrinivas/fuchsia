// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::traits::test_realm_component::TestRealmComponent,
    fuchsia_component_test::{builder::ComponentSource, Moniker},
};

/// A component which can be instantiated from a Fuchsia package.
pub(crate) struct PackagedComponent {
    moniker: Moniker,
    source: ComponentSource,
}

impl PackagedComponent {
    pub(crate) fn new_from_legacy_url(moniker: Moniker, legacy_url: impl Into<String>) -> Self {
        Self { moniker, source: ComponentSource::LegacyUrl(legacy_url.into()) }
    }

    pub(crate) fn new_from_modern_url(moniker: Moniker, modern_url: impl Into<String>) -> Self {
        Self { moniker, source: ComponentSource::Url(modern_url.into()) }
    }
}

impl TestRealmComponent for PackagedComponent {
    fn moniker(&self) -> &Moniker {
        &self.moniker
    }

    fn source(&self) -> ComponentSource {
        self.source.clone()
    }
}
