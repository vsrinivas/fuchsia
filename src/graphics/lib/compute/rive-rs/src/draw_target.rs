// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    component::Component,
    component_dirt::ComponentDirt,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property, TryFromU64},
    drawable::Drawable,
    dyn_vec::DynVec,
    option_cell::OptionCell,
    shapes::ClippingShape,
    status_code::StatusCode,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DrawTargetPlacement {
    Before,
    After,
}

impl Default for DrawTargetPlacement {
    fn default() -> Self {
        Self::Before
    }
}

impl TryFromU64 for DrawTargetPlacement {
    fn try_from(val: u64) -> Option<Self> {
        match val {
            0 => Some(Self::Before),
            1 => Some(Self::After),
            _ => None,
        }
    }
}

#[derive(Debug)]
pub struct DrawTarget {
    component: Component,
    drawable_id: Property<u64>,
    placement: Property<DrawTargetPlacement>,
    drawable: OptionCell<Object<Drawable>>,
    pub(crate) first: OptionCell<Object<Drawable>>,
    pub(crate) last: OptionCell<Object<Drawable>>,
    clipping_shapes: DynVec<Object<ClippingShape>>,
}

impl ObjectRef<'_, DrawTarget> {
    pub fn drawable_id(&self) -> u64 {
        self.drawable_id.get()
    }

    pub fn set_drawable_id(&self, drawable_id: u64) {
        self.drawable_id.set(drawable_id);
    }

    pub fn placement(&self) -> DrawTargetPlacement {
        self.placement.get()
    }

    pub fn set_placement(&self, placement: DrawTargetPlacement) {
        if self.placement() == placement {
            return;
        }

        self.placement.set(placement);

        if let Some(artboard) = self.component.artboard() {
            artboard.cast::<Component>().as_ref().add_dirt(ComponentDirt::DRAW_ORDER, false);
        }
    }
}

impl ObjectRef<'_, DrawTarget> {
    pub fn drawable(&self) -> Option<Object<Drawable>> {
        self.drawable.get()
    }

    pub fn push_clipping_shape(&self, clipping_shape: Object<ClippingShape>) {
        self.clipping_shapes.push(clipping_shape);
    }

    pub fn clipping_shapes(&self) -> impl Iterator<Item = Object<ClippingShape>> + '_ {
        self.clipping_shapes.iter()
    }
}

impl Core for DrawTarget {
    parent_types![(component, Component)];

    properties![(119, drawable_id, set_drawable_id), (120, placement, set_placement), component];
}

impl OnAdded for ObjectRef<'_, DrawTarget> {
    on_added!([on_added_clean, import], Component);

    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        let code = self.cast::<Component>().on_added_dirty(context);
        if code != StatusCode::Ok {
            return code;
        }

        if let Some(drawable) =
            context.resolve(self.drawable_id() as usize).and_then(|core| core.try_cast())
        {
            self.drawable.set(Some(drawable));
            StatusCode::Ok
        } else {
            StatusCode::MissingObject
        }
    }
}

impl Default for DrawTarget {
    fn default() -> Self {
        Self {
            component: Component::default(),
            drawable_id: Property::new(0),
            placement: Property::new(DrawTargetPlacement::Before),
            drawable: OptionCell::new(),
            first: OptionCell::new(),
            last: OptionCell::new(),
            clipping_shapes: DynVec::new(),
        }
    }
}
