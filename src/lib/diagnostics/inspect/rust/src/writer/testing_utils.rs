// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

#[cfg(target_os = "fuchsia")]
mod testing {
    use crate::writer::{Heap, State};
    use mapped_vmo::Mapping;
    use std::sync::Arc;

    pub fn get_state(size: usize) -> State {
        let (mapping, vmo) = Mapping::allocate(size).unwrap();
        let heap = Heap::new(Arc::new(mapping)).unwrap();
        State::create(heap, Arc::new(vmo)).unwrap()
    }
}

#[cfg(not(target_os = "fuchsia"))]
mod testing {
    use crate::writer::{Heap, State};
    use std::sync::{Arc, Mutex};
    use std::vec::Vec;

    pub fn get_state(size: usize) -> State {
        let mut backer = Vec::<u8>::new();
        backer.resize(size, 0);

        let heap = Heap::new(Arc::new(Mutex::new(backer))).unwrap();
        State::create(heap).unwrap()
    }
}

pub use testing::*;
