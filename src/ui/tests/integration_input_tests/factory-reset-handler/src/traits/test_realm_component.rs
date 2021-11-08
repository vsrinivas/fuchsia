// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_component_test::{Moniker, RealmBuilder};

/// A component that runs in the test realm.
#[async_trait::async_trait]
pub(crate) trait TestRealmComponent {
    /// Returns the component's `Moniker`, which gives the component
    /// a name, and a position in the realm's hierarchy of components.
    fn moniker(&self) -> &Moniker;

    /// Adds this component to a realm.
    async fn add_to_builder(&self, builder: &RealmBuilder);
}
