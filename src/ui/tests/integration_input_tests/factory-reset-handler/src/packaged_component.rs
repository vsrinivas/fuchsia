// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::traits::test_realm_component::TestRealmComponent,
    fuchsia_component_test::{ChildProperties, Moniker, RealmBuilder},
};

enum LegacyOrModernUrl {
    LegacyUrl(String),
    ModernUrl(String),
}

/// A component which can be instantiated from a Fuchsia package.
pub(crate) struct PackagedComponent {
    moniker: Moniker,
    source: LegacyOrModernUrl,
}

impl PackagedComponent {
    pub(crate) fn new_from_legacy_url(moniker: Moniker, legacy_url: impl Into<String>) -> Self {
        Self { moniker, source: LegacyOrModernUrl::LegacyUrl(legacy_url.into()) }
    }

    pub(crate) fn new_from_modern_url(moniker: Moniker, modern_url: impl Into<String>) -> Self {
        Self { moniker, source: LegacyOrModernUrl::ModernUrl(modern_url.into()) }
    }
}

#[async_trait::async_trait]
impl TestRealmComponent for PackagedComponent {
    fn moniker(&self) -> &Moniker {
        &self.moniker
    }

    async fn add_to_builder(&self, builder: &RealmBuilder) {
        match &self.source {
            LegacyOrModernUrl::LegacyUrl(url) => {
                builder
                    .add_legacy_child(self.moniker.clone(), url, ChildProperties::new())
                    .await
                    .unwrap();
            }
            LegacyOrModernUrl::ModernUrl(url) => {
                builder.add_child(self.moniker.clone(), url, ChildProperties::new()).await.unwrap();
            }
        }
    }
}
