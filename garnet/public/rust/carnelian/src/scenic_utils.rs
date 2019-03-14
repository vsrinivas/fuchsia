// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::canvas::Color;
use fuchsia_scenic::{Material, SessionPtr, ShapeNode};
use fuchsia_zircon::Time;

/// Type representing times
pub type PresentationTime = Time;

/// Convenience routine to set the color of a scenic node
pub fn set_node_color(session: &SessionPtr, node: &ShapeNode, color: &Color) {
    let material = Material::new(session.clone());
    material.set_color(color.make_color_rgba());
    node.set_material(&material);
}
