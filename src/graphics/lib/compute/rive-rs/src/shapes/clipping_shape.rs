// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{fmt, iter};

use crate::{
    component::Component,
    component_dirt::ComponentDirt,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    drawable::Drawable,
    dyn_vec::DynVec,
    node::Node,
    option_cell::OptionCell,
    shapes::{FillRule, PathSpace, Shape},
    status_code::StatusCode,
    ContainerComponent,
};

use super::{command_path::CommandPathBuilder, CommandPath, ShapePaintContainer};

pub struct ClippingShape {
    component: Component,
    source_id: Property<u64>,
    fill_rule: Property<FillRule>,
    is_visible: Property<bool>,
    shapes: DynVec<Object<Shape>>,
    source: OptionCell<Object<Node>>,
    command_path: OptionCell<CommandPath>,
}

impl fmt::Debug for ClippingShape {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.command_path.with(|command_path| {
            f.debug_struct("ClippingShape")
                .field("component", &self.component)
                .field("source_id", &self.source_id)
                .field("fill_rule", &self.fill_rule)
                .field("is_visible", &self.is_visible)
                .field("shapes", &self.shapes)
                .field("source", &self.source)
                .field("command_path", &command_path)
                .finish()
        })
    }
}

impl ObjectRef<'_, ClippingShape> {
    pub fn source_id(&self) -> u64 {
        self.source_id.get()
    }

    pub fn set_source_id(&self, source_id: u64) {
        self.source_id.set(source_id);
    }

    pub fn fill_rule(&self) -> FillRule {
        self.fill_rule.get()
    }

    pub fn set_fill_rule(&self, fill_rule: FillRule) {
        self.fill_rule.set(fill_rule);
    }

    pub fn is_visible(&self) -> bool {
        self.is_visible.get()
    }

    pub fn set_is_visible(&self, is_visible: bool) {
        self.is_visible.set(is_visible);
    }
}

impl ObjectRef<'_, ClippingShape> {
    pub(crate) fn with_command_path(&self, f: impl FnMut(Option<&CommandPath>)) {
        self.command_path.with(f);
    }

    pub fn build_dependencies(&self) {
        for shape in self.shapes.iter() {
            shape
                .as_ref()
                .path_composer()
                .cast::<Component>()
                .push_dependent(self.as_object().cast());
        }
    }

    pub fn update(&self, value: ComponentDirt) {
        if Component::value_has_dirt(value, ComponentDirt::PATH | ComponentDirt::WORLD_TRANSFORM) {
            let mut builder = CommandPathBuilder::new();

            for shape in self.shapes.iter() {
                shape.as_ref().path_composer().with_world_path(|path| {
                    builder.path(
                        path.expect("world_path should already be set on PathComposer"),
                        None,
                    );
                });
            }

            self.command_path.set(Some(builder.build()));
        }
    }
}

impl Core for ClippingShape {
    parent_types![(component, Component)];

    properties![
        (92, source_id, set_source_id),
        (93, fill_rule, set_fill_rule),
        (94, is_visible, set_is_visible),
        component,
    ];
}

impl OnAdded for ObjectRef<'_, ClippingShape> {
    on_added!([import], Component);

    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        let code = self.cast::<Component>().on_added_dirty(context);
        if code != StatusCode::Ok {
            return code;
        }

        if let Some(node) =
            context.resolve(self.source_id() as usize).and_then(|object| object.try_cast::<Node>())
        {
            self.source.set(Some(node));
            StatusCode::Ok
        } else {
            StatusCode::MissingObject
        }
    }

    fn on_added_clean(&self, context: &dyn CoreContext) -> StatusCode {
        let clipping_holder = self.cast::<Component>().parent();
        let artboard = context.artboard();

        for object in &*artboard.as_ref().objects() {
            // Iterate artboard to find drawables that are parented to this clipping
            // shape, they need to know they'll be clipped by this shape.
            if let Some(drawable) = object.try_cast::<Drawable>() {
                let drawable = drawable.as_ref();
                let mut components = iter::once(drawable.as_object().cast())
                    .chain(drawable.cast::<Component>().parents());

                if components.any(|component| Some(component) == clipping_holder) {
                    drawable.push_clipping_shape(self.as_object());
                }
            }

            // Iterate artboard to find shapes that are parented to the source,
            // their paths will need to be RenderPaths in order to be used for
            // clipping operations.
            if let Some(shape) = object.try_cast::<Shape>() {
                let component = object.cast::<ContainerComponent>();

                if Some(component.clone()) != clipping_holder {
                    let mut parents = iter::once(component.clone())
                        .chain(component.as_ref().cast::<Component>().parents());

                    let source = self.source.get().map(|source| source.cast());

                    if parents.any(|component| Some(component) == source) {
                        shape
                            .as_ref()
                            .cast::<ShapePaintContainer>()
                            .add_default_path_space(PathSpace::WORLD | PathSpace::CLIPPING);
                        self.shapes.push(shape);
                    }
                }
            }
        }

        StatusCode::Ok
    }
}

impl Default for ClippingShape {
    fn default() -> Self {
        Self {
            component: Component::default(),
            source_id: Property::new(0),
            fill_rule: Property::new(FillRule::NonZero),
            is_visible: Property::new(true),
            shapes: DynVec::new(),
            source: OptionCell::new(),
            command_path: OptionCell::new(),
        }
    }
}
