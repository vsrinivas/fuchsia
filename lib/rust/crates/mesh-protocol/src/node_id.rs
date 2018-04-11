// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes::{BufMut, LittleEndian};

// Identification of one device on a mesh: this plays the part of device address
#[derive(PartialEq, Eq, Hash, Clone, PartialOrd, Debug, Copy)]
pub struct NodeId {
    id: u64,
}

impl NodeId {
    pub fn wire_size(&self) -> usize {
        8
    }

    pub fn write<B>(&self, b: &mut B)
    where
        B: BufMut,
    {
        b.put_u64::<LittleEndian>(self.id);
    }
}

impl From<u64> for NodeId {
    fn from(id: u64) -> NodeId {
        NodeId { id }
    }
}
