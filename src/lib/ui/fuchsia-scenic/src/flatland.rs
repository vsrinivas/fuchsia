// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, fuchsia_zircon as zx};

pub use {
    fidl_fuchsia_scenic_scheduling::PresentationInfo,
    fidl_fuchsia_ui_composition::{
        ContentId, ContentLinkMarker, ContentLinkToken, FlatlandDisplayMarker,
        FlatlandDisplayProxy, FlatlandError, FlatlandEvent, FlatlandEventStream, FlatlandMarker,
        FlatlandProxy, GraphLinkMarker, GraphLinkProxy, GraphLinkToken, LayoutInfo, LinkProperties,
        PresentArgs, TransformId,
    },
};

// Pair of tokens used to link two Flatland sessions together.
pub struct LinkTokenPair {
    pub graph_link_token: GraphLinkToken,
    pub content_link_token: ContentLinkToken,
}

impl LinkTokenPair {
    pub fn new() -> Result<LinkTokenPair, Error> {
        let (graph_link_token, content_link_token) = zx::Channel::create()?;
        Ok(LinkTokenPair {
            graph_link_token: GraphLinkToken { value: graph_link_token },
            content_link_token: ContentLinkToken { value: content_link_token },
        })
    }
}
