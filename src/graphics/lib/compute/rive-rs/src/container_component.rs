// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    component::Component,
    core::{Core, ObjectRef, OnAdded},
};

#[derive(Debug, Default)]
pub struct ContainerComponent {
    component: Component,
}

impl Core for ContainerComponent {
    parent_types![(component, Component)];

    properties!(component);
}

impl OnAdded for ObjectRef<'_, ContainerComponent> {
    on_added!(Component);
}
