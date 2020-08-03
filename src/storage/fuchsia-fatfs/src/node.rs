// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{directory::FatDirectory, file::FatFile, filesystem::FatFilesystemInner},
    fuchsia_zircon::Status,
    std::sync::{Arc, Weak},
};

#[derive(Clone, Debug)]
/// This enum is used to represent values which could be either a FatDirectory
/// or a FatFile. This holds a strong reference to the contained file/directory.
pub enum FatNode {
    Dir(Arc<FatDirectory>),
    File(Arc<FatFile>),
}

impl FatNode {
    /// Downgrade this FatNode into a WeakFatNode.
    pub fn downgrade(&self) -> WeakFatNode {
        match self {
            FatNode::Dir(a) => WeakFatNode::Dir(Arc::downgrade(a)),
            FatNode::File(b) => WeakFatNode::File(Arc::downgrade(b)),
        }
    }

    /// Attach this FatNode to the given FatDirectory, with the given name.
    pub fn attach(
        &self,
        parent: Arc<FatDirectory>,
        name: &str,
        fs: &FatFilesystemInner,
    ) -> Result<(), Status> {
        match self {
            FatNode::Dir(a) => a.attach(parent, name, fs),
            FatNode::File(b) => b.attach(parent, name, fs),
        }
    }

    /// Detach this FatNode from its parent.
    pub fn detach(&self, fs: &FatFilesystemInner) {
        match self {
            FatNode::Dir(a) => a.detach(fs),
            FatNode::File(b) => b.detach(fs),
        }
    }
}

/// The same as FatNode, but using a weak reference.
#[derive(Debug)]
pub enum WeakFatNode {
    Dir(Weak<FatDirectory>),
    File(Weak<FatFile>),
}

impl WeakFatNode {
    /// Try and upgrade this WeakFatNode to a FatNode. Returns None
    /// if the referenced object has been destroyed.
    pub fn upgrade(&self) -> Option<FatNode> {
        match self {
            WeakFatNode::Dir(a) => a.upgrade().map(|val| FatNode::Dir(val)),
            WeakFatNode::File(b) => b.upgrade().map(|val| FatNode::File(val)),
        }
    }
}
