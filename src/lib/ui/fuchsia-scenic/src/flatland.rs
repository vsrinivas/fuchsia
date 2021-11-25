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
        TransformId, ViewBoundProtocols, ViewportProperties,
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

/// IdGenerator generates a monotonically-increasing sequence of IDs, either TransformIds or
/// ContentIds, depending on what the caller asks for.  The ID values are unique both across and
/// within ID types, e.g. a given IdGenerator will not generate two TransformIds with the same
/// value, nor a TransformId and a ContentId with the same value.
pub struct IdGenerator {
    next_id: u64,
}

impl IdGenerator {
    pub fn new() -> Self {
        IdGenerator { next_id: 1 }
    }

    pub fn new_with_first_id(first_id: u64) -> Self {
        IdGenerator { next_id: first_id }
    }

    pub fn next_transform_id(&mut self) -> TransformId {
        let id = self.next_id;
        self.next_id += 1;
        TransformId { value: id }
    }

    pub fn next_content_id(&mut self) -> ContentId {
        let id = self.next_id;
        self.next_id += 1;
        ContentId { value: id }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn id_generator_basic() {
        let mut generator = IdGenerator::new();
        assert_eq!(generator.next_content_id(), ContentId { value: 1 });
        assert_eq!(generator.next_content_id(), ContentId { value: 2 });
        assert_eq!(generator.next_transform_id(), TransformId { value: 3 });
        assert_eq!(generator.next_transform_id(), TransformId { value: 4 });
    }

    #[test]
    fn id_generator_with_first_id() {
        let mut generator = IdGenerator::new_with_first_id(11);
        assert_eq!(generator.next_transform_id(), TransformId { value: 11 });
    }
}
