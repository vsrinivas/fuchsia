// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::server::directory::FxDirectory,
    std::sync::{Arc, Weak},
};

// TODO(jfsulliv): Should FxNode be a trait object instead of an enum, so that we can have common
// functionality shared between directories and files?

#[derive(Clone)]
// FxNode is a strong reference to a node in the filesystem hierarchy (either a file or directory).
pub enum FxNode {
    Dir(Arc<FxDirectory>),
}

impl FxNode {
    /// Creates a weak copy of self.
    pub fn downgrade(&self) -> WeakFxNode {
        match self {
            FxNode::Dir(dir) => WeakFxNode::Dir(Arc::downgrade(dir)),
        }
    }
}

#[derive(Clone)]
// WeakFxNode is a weak version of FxNode.
pub enum WeakFxNode {
    Dir(Weak<FxDirectory>),
}

impl WeakFxNode {
    /// Attempts to create a strong copy of self. If the underlying node has been deleted, returns
    /// None.
    pub fn upgrade(&self) -> Option<FxNode> {
        match self {
            WeakFxNode::Dir(dir) => Weak::upgrade(dir).map(|dir| FxNode::Dir(dir)),
        }
    }
}
