// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::traits::test_realm_component::TestRealmComponent,
    fuchsia_component_test::{ChildOptions, RealmBuilder, Ref},
};

enum LegacyOrModernUrl {
    ModernUrl(String),
}

/// A component which can be instantiated from a Fuchsia package.
pub(crate) struct PackagedComponent {
    name: String,
    source: LegacyOrModernUrl,
}

impl PackagedComponent {
    pub(crate) fn new_from_modern_url(
        name: impl Into<String>,
        modern_url: impl Into<String>,
    ) -> Self {
        Self { name: name.into(), source: LegacyOrModernUrl::ModernUrl(modern_url.into()) }
    }
}

#[async_trait::async_trait]
impl TestRealmComponent for PackagedComponent {
    fn ref_(&self) -> Ref {
        Ref::child(&self.name)
    }

    async fn add_to_builder(&self, builder: &RealmBuilder) {
        match &self.source {
            LegacyOrModernUrl::ModernUrl(url) => {
                builder.add_child(&self.name, url, ChildOptions::new()).await.unwrap();
            }
        }
    }
}
