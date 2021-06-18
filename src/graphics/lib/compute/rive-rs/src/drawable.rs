// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    core::{Core, Object, ObjectRef, OnAdded, Property},
    draw_rules::DrawRules,
    dyn_vec::DynVec,
    math::Mat,
    node::Node,
    option_cell::OptionCell,
    shapes::{paint::BlendMode, ClippingShape, CommandPath, CommandPathBuilder, Shape},
    Renderer,
};

#[derive(Debug)]
pub struct Drawable {
    node: Node,
    blend_mode: Property<BlendMode>,
    drawable_flags: Property<u64>,
    clipping_shapes: DynVec<Object<ClippingShape>>,
    pub(crate) flattened_draw_rules: OptionCell<Object<DrawRules>>,
    pub(crate) prev: OptionCell<Object<Self>>,
    pub(crate) next: OptionCell<Object<Self>>,
}

impl ObjectRef<'_, Drawable> {
    pub fn blend_mode(&self) -> BlendMode {
        self.blend_mode.get()
    }

    pub fn set_blend_mode(&self, blend_mode: BlendMode) {
        self.blend_mode.set(blend_mode);
    }

    pub fn drawable_flags(&self) -> u64 {
        self.drawable_flags.get()
    }

    pub fn set_drawable_flags(&self, drawable_flags: u64) {
        self.drawable_flags.set(drawable_flags);
    }
}

impl ObjectRef<'_, Drawable> {
    pub fn push_clipping_shape(&self, clipping_shape: Object<ClippingShape>) {
        self.clipping_shapes.push(clipping_shape);
    }

    pub fn clip(&self) -> Option<CommandPath> {
        self.clipping_shapes
            .iter()
            .fold(None, |mut option, clipping_shape| {
                let builder = option.get_or_insert_with(|| CommandPathBuilder::new());

                clipping_shape.as_ref().with_command_path(|path| {
                    if let Some(path) = path {
                        builder.path(path, None);
                    }
                });

                option
            })
            .map(|builder| builder.build())
    }

    pub fn draw(&self, renderer: &mut impl Renderer, transform: Mat) {
        if let Some(shape) = self.try_cast::<Shape>() {
            return shape.draw(renderer, transform);
        }

        unreachable!()
    }
}

impl Core for Drawable {
    parent_types![(node, Node)];

    properties![(23, blend_mode, set_blend_mode), (129, drawable_flags, set_drawable_flags), node];
}

impl OnAdded for ObjectRef<'_, Drawable> {
    on_added!(Node);
}

impl Default for Drawable {
    fn default() -> Self {
        Self {
            node: Node::default(),
            blend_mode: Property::new(BlendMode::SrcOver),
            drawable_flags: Property::new(0),
            clipping_shapes: DynVec::new(),
            flattened_draw_rules: OptionCell::new(),
            prev: OptionCell::new(),
            next: OptionCell::new(),
        }
    }
}
