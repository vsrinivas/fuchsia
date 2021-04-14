// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    core::{Core, ObjectRef, OnAdded},
    transform_component::TransformComponent,
};

#[derive(Debug, Default)]
pub struct SkeletalComponent {
    transform_component: TransformComponent,
}

impl Core for SkeletalComponent {
    parent_types![(transform_component, TransformComponent)];

    properties!(transform_component);
}

impl OnAdded for ObjectRef<'_, SkeletalComponent> {
    on_added!(TransformComponent);
}
