// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fuchsia_component::server::{ServiceFs, ServiceObjTrait};
use fuchsia_inspect::vmo::Inspector;
use parking_lot::Mutex;
use std::sync::Arc;
use wlan_inspect::iface_mgr::{IfaceTreeHolder, IfacesTrees};

const VMO_SIZE_BYTES: usize = 1000 * 1024;
const MAX_DEAD_IFACE_NODES: usize = 2;

pub struct WlanstackTree {
    inspector: Inspector,
    ifaces_trees: Mutex<IfacesTrees>,
}

impl WlanstackTree {
    pub fn new<ServiceObjTy: ServiceObjTrait>(
        fs: &mut ServiceFs<ServiceObjTy>,
    ) -> Result<Self, Error> {
        let inspector = Inspector::new_with_size(VMO_SIZE_BYTES)?;
        inspector.export(fs)?;
        let ifaces_trees = IfacesTrees::new(MAX_DEAD_IFACE_NODES);
        Ok(Self { inspector, ifaces_trees: Mutex::new(ifaces_trees) })
    }

    pub fn create_iface_child(&self, iface_id: u16) -> Arc<IfaceTreeHolder> {
        self.ifaces_trees.lock().create_iface_child(self.inspector.root(), iface_id)
    }

    pub fn notify_iface_removed(&self, iface_id: u16) {
        self.ifaces_trees.lock().notify_iface_removed(iface_id)
    }
}
