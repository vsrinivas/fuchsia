// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::writer::{Error, Heap, InspectTypeInternal, Node, State};
use diagnostics_hierarchy::{testing::DiagnosticsHierarchyGetter, DiagnosticsHierarchy};
use fuchsia_zircon::{self as zx, HandleBased};
use inspect_format::constants;
use mapped_vmo::Mapping;
use std::{borrow::Cow, cmp::max, sync::Arc};
use tracing::error;

/// Root of the Inspect API. Through this API, further nodes can be created and inspect can be
/// served.
#[derive(Clone)]
pub struct Inspector {
    /// The root node.
    root_node: Arc<Node>,

    /// The VMO backing the inspector
    pub(in crate) vmo: Option<Arc<zx::Vmo>>,
}

impl DiagnosticsHierarchyGetter<String> for Inspector {
    fn get_diagnostics_hierarchy(&self) -> Cow<'_, DiagnosticsHierarchy> {
        let hierarchy = futures::executor::block_on(async move { crate::reader::read(self).await })
            .expect("failed to get hierarchy");
        Cow::Owned(hierarchy)
    }
}

impl Inspector {
    /// Initializes a new Inspect VMO object with the
    /// [`default maximum size`][constants::DEFAULT_VMO_SIZE_BYTES].
    pub fn new() -> Self {
        Inspector::new_with_size(constants::DEFAULT_VMO_SIZE_BYTES)
    }

    /// True if the Inspector was created successfully (it's not No-Op)
    pub fn is_valid(&self) -> bool {
        self.vmo.is_some() && self.root_node.is_valid()
    }

    /// Initializes a new Inspect VMO object with the given maximum size. If the
    /// given size is less than 4K, it will be made 4K which is the minimum size
    /// the VMO should have.
    pub fn new_with_size(max_size: usize) -> Self {
        match Inspector::new_root(max_size) {
            Ok((vmo, root_node)) => {
                Inspector { vmo: Some(Arc::new(vmo)), root_node: Arc::new(root_node) }
            }
            Err(e) => {
                error!("Failed to create root node. Error: {:?}", e);
                Inspector::new_no_op()
            }
        }
    }

    /// Returns a duplicate of the underlying VMO for this Inspector.
    ///
    /// The duplicated VMO will be read-only, and is suitable to send to clients over FIDL.
    pub fn duplicate_vmo(&self) -> Option<zx::Vmo> {
        self.vmo
            .as_ref()
            .map(|vmo| {
                vmo.duplicate_handle(zx::Rights::BASIC | zx::Rights::READ | zx::Rights::MAP).ok()
            })
            .unwrap_or(None)
    }

    /// Returns a VMO holding a copy of the data in this inspector.
    ///
    /// The copied VMO will be read-only.
    pub fn copy_vmo(&self) -> Option<zx::Vmo> {
        self.copy_vmo_data().and_then(|data| {
            if let Ok(vmo) = zx::Vmo::create(data.len() as u64) {
                vmo.write(&data, 0).ok().map(|_| vmo)
            } else {
                None
            }
        })
    }

    /// Returns a copy of the bytes stored in the VMO for this inspector.
    ///
    /// The output will be truncated to only those bytes that are needed to accurately read the
    /// stored data.
    pub fn copy_vmo_data(&self) -> Option<Vec<u8>> {
        self.root_node.inner.inner_ref().map(|inner_ref| inner_ref.state.copy_vmo_bytes())
    }

    /// Returns the root node of the inspect hierarchy.
    pub fn root(&self) -> &Node {
        &self.root_node
    }

    /// Takes a function to execute as under a single lock of the Inspect VMO. This function
    /// receives a reference to the root of the inspect hierarchy.
    pub fn atomic_update<F, R>(&self, update_fn: F) -> R
    where
        F: FnMut(&Node) -> R,
    {
        self.root().atomic_update(update_fn)
    }

    /// Creates a new No-Op inspector
    pub fn new_no_op() -> Self {
        Inspector { vmo: None, root_node: Arc::new(Node::new_no_op()) }
    }

    /// Allocates a new VMO and initializes it.
    fn new_root(max_size: usize) -> Result<(zx::Vmo, Node), Error> {
        let mut size = max(constants::MINIMUM_VMO_SIZE_BYTES, max_size);
        // If the size is not a multiple of 4096, round up.
        if size % constants::MINIMUM_VMO_SIZE_BYTES != 0 {
            size =
                (1 + size / constants::MINIMUM_VMO_SIZE_BYTES) * constants::MINIMUM_VMO_SIZE_BYTES;
        }
        let (mapping, vmo) = Mapping::allocate_with_name(size, "InspectHeap")
            .map_err(|status| Error::AllocateVmo(status))?;
        let heap = Heap::new(Arc::new(mapping)).map_err(|e| Error::CreateHeap(Box::new(e)))?;
        let state = State::create(heap).map_err(|e| Error::CreateState(Box::new(e)))?;
        Ok((vmo, Node::new_root(state)))
    }

    /// Creates an no-op inspector from the given Vmo. If the VMO is corrupted, reading can fail.
    pub fn no_op_from_vmo(vmo: Arc<zx::Vmo>) -> Inspector {
        Inspector { vmo: Some(vmo), root_node: Arc::new(Node::new_no_op()) }
    }

    pub(crate) fn state(&self) -> Option<State> {
        self.root().inner.inner_ref().map(|inner_ref| inner_ref.state.clone())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_zircon::AsHandleRef;
    use std::ffi::CString;

    #[test]
    fn inspector_new() {
        let test_object = Inspector::new();
        assert_eq!(
            test_object.vmo.as_ref().unwrap().get_size().unwrap(),
            constants::DEFAULT_VMO_SIZE_BYTES as u64
        );
    }

    #[test]
    fn inspector_duplicate_vmo() {
        let test_object = Inspector::new();
        assert_eq!(
            test_object.vmo.as_ref().unwrap().get_size().unwrap(),
            constants::DEFAULT_VMO_SIZE_BYTES as u64
        );
        assert_eq!(
            test_object.duplicate_vmo().unwrap().get_size().unwrap(),
            constants::DEFAULT_VMO_SIZE_BYTES as u64
        );
    }

    #[test]
    fn inspector_copy_data() {
        let test_object = Inspector::new();

        assert_eq!(
            test_object.vmo.as_ref().unwrap().get_size().unwrap(),
            constants::DEFAULT_VMO_SIZE_BYTES as u64
        );
        // The copy will be a single page, since that is all that is used.
        assert_eq!(test_object.copy_vmo_data().unwrap().len(), 4096);
    }

    #[test]
    fn no_op() {
        let inspector = Inspector::new_with_size(4096);
        // Make the VMO full.
        let nodes = (0..127).map(|_| inspector.root().create_child("test")).collect::<Vec<Node>>();

        assert!(nodes.iter().all(|node| node.is_valid()));
        let no_op_node = inspector.root().create_child("no-op-child");
        assert!(!no_op_node.is_valid());
    }

    #[test]
    fn inspector_new_with_size() {
        let test_object = Inspector::new_with_size(8192);
        assert_eq!(test_object.vmo.as_ref().unwrap().get_size().unwrap(), 8192);
        assert_eq!(
            CString::new("InspectHeap").unwrap(),
            test_object.vmo.as_ref().unwrap().get_name().expect("Has name")
        );

        // If size is not a multiple of 4096, it'll be rounded up.
        let test_object = Inspector::new_with_size(10000);
        assert_eq!(test_object.vmo.unwrap().get_size().unwrap(), 12288);

        // If size is less than the minimum size, the minimum will be set.
        let test_object = Inspector::new_with_size(2000);
        assert_eq!(test_object.vmo.unwrap().get_size().unwrap(), 4096);
    }

    #[test]
    fn inspector_new_root() {
        // Note, the small size we request should be rounded up to a full 4kB page.
        let (vmo, root_node) = Inspector::new_root(100).unwrap();
        assert_eq!(vmo.get_size().unwrap(), 4096);
        let inner = root_node.inner.inner_ref().unwrap();
        assert_eq!(inner.block_index, 0);
        assert_eq!(CString::new("InspectHeap").unwrap(), vmo.get_name().expect("Has name"));
    }
}
