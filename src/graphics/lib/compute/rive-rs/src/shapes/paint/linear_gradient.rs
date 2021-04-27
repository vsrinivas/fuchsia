// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::Cell, convert::TryFrom};

use crate::{
    component::Component,
    component_dirt::ComponentDirt,
    container_component::ContainerComponent,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    dyn_vec::DynVec,
    math,
    node::Node,
    option_cell::OptionCell,
    renderer::{GradientBuilder, GradientType},
    shapes::{
        paint::{shape_paint_mutator::ShapePaintMutator, GradientStop, RadialGradient},
        ShapePaintContainer,
    },
    status_code::StatusCode,
    TransformComponent,
};

#[derive(Debug)]
pub struct LinearGradient {
    container_component: ContainerComponent,
    shape_paint_mutator: ShapePaintMutator,
    start_x: Property<f32>,
    start_y: Property<f32>,
    end_x: Property<f32>,
    end_y: Property<f32>,
    opacity: Property<f32>,
    stops: DynVec<Object<GradientStop>>,
    paints_in_world_space: Cell<bool>,
    shape_paint_container: OptionCell<Object<Node>>,
}

impl ObjectRef<'_, LinearGradient> {
    pub fn start_x(&self) -> f32 {
        self.start_x.get()
    }

    pub fn set_start_x(&self, start_x: f32) {
        if self.start_x() == start_x {
            return;
        }

        self.start_x.set(start_x);
        self.cast::<Component>().add_dirt(ComponentDirt::TRANSFORM, false);
    }

    pub fn start_y(&self) -> f32 {
        self.start_y.get()
    }

    pub fn set_start_y(&self, start_y: f32) {
        if self.start_y() == start_y {
            return;
        }

        self.start_y.set(start_y);
        self.cast::<Component>().add_dirt(ComponentDirt::TRANSFORM, false);
    }

    pub fn end_x(&self) -> f32 {
        self.end_x.get()
    }

    pub fn set_end_x(&self, end_x: f32) {
        if self.end_x() == end_x {
            return;
        }

        self.end_x.set(end_x);
        self.cast::<Component>().add_dirt(ComponentDirt::TRANSFORM, false);
    }

    pub fn end_y(&self) -> f32 {
        self.end_y.get()
    }

    pub fn set_end_y(&self, end_y: f32) {
        if self.end_y() == end_y {
            return;
        }

        self.end_y.set(end_y);
        self.cast::<Component>().add_dirt(ComponentDirt::TRANSFORM, false);
    }

    pub fn opacity(&self) -> f32 {
        self.opacity.get()
    }

    pub fn set_opacity(&self, opacity: f32) {
        if self.opacity() == opacity {
            return;
        }

        self.opacity.set(opacity);
        self.mark_gradient_dirty();
    }
}

impl ObjectRef<'_, LinearGradient> {
    pub fn push_stop(&self, stop: Object<GradientStop>) {
        self.stops.push(stop);
    }

    pub fn paints_in_world_space(&self) -> bool {
        self.paints_in_world_space.get()
    }

    pub fn set_paints_in_world_space(&self, paints_in_world_space: bool) {
        if self.paints_in_world_space() != paints_in_world_space {
            self.paints_in_world_space.set(paints_in_world_space);
            self.cast::<Component>().add_dirt(ComponentDirt::PAINT, false);
        }
    }

    pub fn mark_gradient_dirty(&self) {
        self.cast::<Component>().add_dirt(ComponentDirt::PAINT, false);
    }

    pub fn mark_stops_dirty(&self) {
        self.cast::<Component>().add_dirt(ComponentDirt::PAINT & ComponentDirt::STOPS, false);
    }

    pub fn build_dependencies(&self) {
        let component = self.cast::<Component>();
        let parent = component.parent();

        if let Some(parents_parent) =
            parent.and_then(|parent| parent.cast::<Component>().as_ref().parent())
        {
            // Parent's parent must be a shape paint container.
            let object: Object = Object::from(parents_parent.clone());
            assert!(Object::<ShapePaintContainer>::try_from(object).is_ok());

            // TODO: see if artboard should inherit from some TransformComponent
            // that can return a world transform. We store the container just for
            // doing the transform to world in update. If it's the artboard, then
            // we're already in world so no need to transform.
            self.shape_paint_container.set(parents_parent.try_cast::<Node>());
            parents_parent.cast::<Component>().as_ref().push_dependent(component.as_object());
        }
    }

    pub fn update(&self, value: ComponentDirt) {
        let mut builder = GradientBuilder::new(match self.try_cast::<RadialGradient>() {
            Some(_) => GradientType::Radial,
            None => GradientType::Linear,
        });

        // Do the stops need to be re-ordered?
        if Component::value_has_dirt(value, ComponentDirt::STOPS) {
            self.stops
                .sort_by(|a, b| a.as_ref().position().partial_cmp(&b.as_ref().position()).unwrap());
        }

        let world_transformed = Component::value_has_dirt(value, ComponentDirt::WORLD_TRANSFORM);
        let rebuild_gradient = Component::value_has_dirt(
            value,
            ComponentDirt::PAINT | ComponentDirt::RENDER_OPACITY | ComponentDirt::TRANSFORM,
        ) || self.paints_in_world_space() && world_transformed;

        if !rebuild_gradient {
            return;
        }

        let start = math::Vec::new(self.start_x(), self.start_y());
        let end = math::Vec::new(self.end_x(), self.end_y());

        // Check if we need to update the world space gradient (if there's no
        // shape container, presumably it's the artboard and we're already in
        // world).
        match (self.paints_in_world_space(), self.shape_paint_container.get()) {
            (true, Some(shape_paint_container)) => {
                // Get the start and end of the gradient in world coordinates (world
                // transform of the shape).
                let world =
                    shape_paint_container.cast::<TransformComponent>().as_ref().world_transform();
                let world_start = world * start;
                let world_end = world * end;

                builder.start(world_start).end(world_end);
            }
            _ => {
                builder.start(start).end(end);
            }
        }

        let ro = self.opacity() * self.cast::<ShapePaintMutator>().render_opacity();
        for stop in self.stops.iter() {
            builder.push_stop(stop.as_ref().color().mul_opacity(ro), stop.as_ref().position());
        }

        self.cast::<ShapePaintMutator>().set_gradient(builder.build());
    }
}

impl Core for LinearGradient {
    parent_types![
        (container_component, ContainerComponent),
        (shape_paint_mutator, ShapePaintMutator),
    ];

    properties![
        (42, start_x, set_start_x),
        (33, start_y, set_start_y),
        (34, end_x, set_end_x),
        (35, end_y, set_end_y),
        (46, opacity, set_opacity),
        container_component,
    ];
}

impl OnAdded for ObjectRef<'_, LinearGradient> {
    on_added!([on_added_clean, import], ContainerComponent);

    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        let code = self.cast::<ContainerComponent>().on_added_dirty(context);
        if code != StatusCode::Ok {
            return code;
        }

        if let Some(parent) =
            self.cast::<Component>().parent().and_then(|parent| parent.try_cast::<Component>())
        {
            self.cast::<ShapePaintMutator>().init_paint_mutator(parent);
            StatusCode::Ok
        } else {
            StatusCode::MissingObject
        }
    }
}

impl Default for LinearGradient {
    fn default() -> Self {
        Self {
            container_component: ContainerComponent::default(),
            shape_paint_mutator: ShapePaintMutator::default(),
            start_x: Property::new(0.0),
            start_y: Property::new(0.0),
            end_x: Property::new(0.0),
            end_y: Property::new(0.0),
            opacity: Property::new(1.0),
            stops: DynVec::new(),
            paints_in_world_space: Cell::new(false),
            shape_paint_container: OptionCell::new(),
        }
    }
}
