// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error};
use mapped_vmo::Mapping;
use parking_lot::Mutex;
use std::rc::Rc;
use std::sync::Arc;

use crate::vmo::constants;
use crate::vmo::heap::Heap;
use crate::vmo::state::State;
use crate::vmo::types::Node;

/// Root of the Inspect API
pub struct Inspector {
    /// The root node.
    root_node: Node,
}

impl Inspector {
    /// Create a new Inspect VMO object with the given maximum size.
    pub fn new(max_size: usize, name: &str) -> Result<Self, Error> {
        let (mapping, _) = Mapping::allocate(max_size)
            .map_err(|e| format_err!("failed to allocate vmo zx status={}", e))?;
        let heap = Heap::new(Rc::new(mapping))?;
        let state = State::create(heap)?;
        let root_node =
            Node::allocate(Arc::new(Mutex::new(state)), name, constants::ROOT_PARENT_INDEX)?;
        Ok(Inspector { root_node })
    }

    /// Create the root of the VMO object with the given |name|.
    pub fn root(&self) -> &Node {
        &self.root_node
    }
}
