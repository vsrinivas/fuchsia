// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    core::{Core, ObjectRef, OnAdded},
    shapes::paint::LinearGradient,
};

#[derive(Debug, Default)]
pub struct RadialGradient {
    linear_gradient: LinearGradient,
}

impl Core for RadialGradient {
    parent_types![(linear_gradient, LinearGradient)];

    properties!(linear_gradient);
}

impl OnAdded for ObjectRef<'_, RadialGradient> {
    on_added!(LinearGradient);
}
