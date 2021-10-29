// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_component_test::{builder::ComponentSource, Moniker};

/// A component that runs in the test realm.
pub(crate) trait TestRealmComponent {
    /// Returns the component's `Moniker`, which gives the component
    /// a name, and a position in the realm's hierarchy of components.
    fn moniker(&self) -> &Moniker;

    /// Returns the component's source. For example, this might be
    /// a URL to a Fuchsia package, or it might be a local function
    /// that implements a mock service.
    fn source(&self) -> ComponentSource;
}
