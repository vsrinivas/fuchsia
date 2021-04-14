// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitflags::bitflags;

bitflags! {
    #[derive(Default)]
    pub struct ComponentDirt: u16 {
        const DEPENDENTS = 0b0000000001;
        /// General flag for components are dirty (if this is up, the update
        /// cycle runs). It gets automatically applied with any other dirt.
        const COMPONENTS = 0b0000000010;
        /// Draw order needs to be re-computed.
        const DRAW_ORDER = 0b0000000100;
        /// Path is dirty and needs to be rebuilt.
        const PATH = 0b0000001000;
        /// Vertices have changed, re-order cached lists.
        const VERTICES = 0b0000010000;
        /// Used by any component that needs to recompute their local transform.
        /// Usually components that have their transform dirty will also have
        /// their worldTransform dirty.
        const TRANSFORM = 0b0000100000;
        /// Used by any component that needs to update its world transform.
        const WORLD_TRANSFORM = 0b0001000000;
        /// Marked when the stored render opacity needs to be updated.
        const RENDER_OPACITY = 0b0010000000;
        /// Dirt used to mark some stored paint needs to be rebuilt or that we
        /// just want to trigger an update cycle so painting occurs.
        const PAINT = 0b0100000000;
        /// Used by the gradients track when the stops need to be re-ordered.
        const STOPS = 0b1000000000;
    }
}
