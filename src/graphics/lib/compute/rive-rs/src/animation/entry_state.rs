// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::LayerState,
    core::{Core, ObjectRef, OnAdded},
};

#[derive(Debug, Default)]
pub struct EntryState {
    layer_state: LayerState,
}

impl Core for EntryState {
    parent_types![(layer_state, LayerState)];

    properties!(layer_state);
}

impl OnAdded for ObjectRef<'_, EntryState> {
    on_added!(LayerState);
}
