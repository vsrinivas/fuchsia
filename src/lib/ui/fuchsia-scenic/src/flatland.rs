// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, fuchsia_zircon as zx};

pub use {
    fidl_fuchsia_scenic_scheduling::PresentationInfo,
    fidl_fuchsia_ui_composition::{
        ChildViewWatcherMarker, ContentId, FlatlandDisplayMarker, FlatlandDisplayProxy,
        FlatlandError, FlatlandEvent, FlatlandEventStream, FlatlandMarker, FlatlandProxy,
        LayoutInfo, ParentViewportWatcherMarker, ParentViewportWatcherProxy, PresentArgs,
        TransformId, ViewportProperties,
    },
    fidl_fuchsia_ui_views::{ViewCreationToken, ViewportCreationToken},
};

// Pair of tokens used to link two Flatland sessions together.
pub struct LinkTokenPair {
    pub view_creation_token: ViewCreationToken,
    pub viewport_creation_token: ViewportCreationToken,
}

impl LinkTokenPair {
    pub fn new() -> Result<LinkTokenPair, Error> {
        let (view_creation_token, viewport_creation_token) = zx::Channel::create()?;
        Ok(LinkTokenPair {
            view_creation_token: ViewCreationToken { value: view_creation_token },
            viewport_creation_token: ViewportCreationToken { value: viewport_creation_token },
        })
    }
}
