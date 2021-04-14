// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    core::{Core, ObjectRef, OnAdded},
    shapes::Polygon,
};

#[derive(Debug, Default)]
pub struct Star {
    polygon: Polygon,
}

impl Core for Star {
    parent_types![(polygon, Polygon)];

    properties![polygon];
}

impl OnAdded for ObjectRef<'_, Star> {
    on_added!(Polygon);
}
