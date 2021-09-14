// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use crate::writer::{Heap, State};
use mapped_vmo::Mapping;
use std::sync::Arc;

pub fn get_state(size: usize) -> State {
    let (mapping, _) = Mapping::allocate(size).unwrap();
    let heap = Heap::new(Arc::new(mapping)).unwrap();
    State::create(heap).unwrap()
}
