// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::Cell, convert::TryFrom};

use crate::{
    core::{Core, Object, ObjectRef, OnAdded},
    dyn_vec::DynVec,
    shapes::{
        paint::{ShapePaint, Stroke},
        PathSpace, Shape,
    },
    Artboard,
};

#[derive(Debug, Default)]
pub struct ShapePaintContainer {
    default_path_space: Cell<PathSpace>,
    shape_paints: DynVec<Object<ShapePaint>>,
}

impl ShapePaintContainer {
    pub fn push_paint(&self, shape_paint: Object<ShapePaint>) {
        self.shape_paints.push(shape_paint);
    }

    pub(crate) fn shape_paints(&self) -> impl Iterator<Item = Object<ShapePaint>> + '_ {
        self.shape_paints.iter()
    }

    pub fn path_space(&self) -> PathSpace {
        self.shape_paints
            .iter()
            .map(|shape_paint| shape_paint.as_ref().path_space())
            .fold(self.default_path_space.get(), |a, e| a | e)
    }

    pub fn add_default_path_space(&self, space: PathSpace) {
        self.default_path_space.set(self.default_path_space.get() | space);
    }

    pub fn invalidate_stroke_effects(&self) {
        for paint in self.shape_paints.iter() {
            if let Some(stroke) = paint.try_cast::<Stroke>() {
                stroke.as_ref().invalidate_effects();
            }
        }
    }

    pub fn make_command_path(&self, _space: PathSpace) {
        // let mut needs_render =
        //     ((space | self.default_path_space.get()) & PathSpace::CLIPPING) == PathSpace::CLIPPING;
        // let mut needs_effects = false;

        // for paint in self.shape_paints.iter() {
        //     if !space.is_empty() && (space & paint.as_ref().path_space()) != space {
        //         continue;
        //     }

        //     match paint.try_cast::<Stroke>() {
        //         Some(stroke) if stroke.as_ref().has_stroke_effect() => needs_effects = true,
        //         _ => needs_render = true,
        //     }
        // }

        // if needs_render && needs_effects {
        //     Box::new(MetricsPath)
        // } else if needs_effects {
        //     Box::new(MetricsPath)
        // } else {
        //     Box::new(RenderPath)
        // }

        todo!();
    }
}

impl TryFrom<Object> for Object<ShapePaintContainer> {
    type Error = ();

    fn try_from(value: Object) -> Result<Self, Self::Error> {
        if let Some(artboard) = value.try_cast::<Artboard>() {
            return Ok(artboard.cast());
        }

        if let Some(shape) = value.try_cast::<Shape>() {
            return Ok(shape.cast());
        }

        Err(())
    }
}

impl Core for ShapePaintContainer {}

impl OnAdded for ObjectRef<'_, ShapePaintContainer> {
    on_added!();
}
