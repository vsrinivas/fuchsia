// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::Cell;

use crate::{
    bones::{Bone, RootBone},
    component_dirt::ComponentDirt,
    container_component::ContainerComponent,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    math::{self, Mat},
    node::Node,
    option_cell::OptionCell,
    Component, StatusCode,
};

#[derive(Debug)]
pub struct TransformComponent {
    container_component: ContainerComponent,
    rotation: Property<f32>,
    scale_x: Property<f32>,
    scale_y: Property<f32>,
    opacity: Property<f32>,
    transform: Cell<Mat>,
    world_transform: Cell<Mat>,
    render_opacity: Cell<f32>,
    parent_transform_component: OptionCell<Object<Self>>,
}

impl ObjectRef<'_, TransformComponent> {
    pub fn rotation(&self) -> f32 {
        self.rotation.get()
    }

    pub fn set_rotation(&self, rotation: f32) {
        if self.rotation() == rotation {
            return;
        }

        self.rotation.set(rotation);
        self.mark_transform_dirty();
    }

    pub fn scale_x(&self) -> f32 {
        self.scale_x.get()
    }

    pub fn set_scale_x(&self, scale_x: f32) {
        if self.scale_x() == scale_x {
            return;
        }

        self.scale_x.set(scale_x);
        self.mark_transform_dirty();
    }

    pub fn scale_y(&self) -> f32 {
        self.scale_y.get()
    }

    pub fn set_scale_y(&self, scale_y: f32) {
        if self.scale_y() == scale_y {
            return;
        }

        self.scale_y.set(scale_y);
        self.mark_transform_dirty();
    }

    pub fn opacity(&self) -> f32 {
        self.opacity.get()
    }

    pub fn set_opacity(&self, opacity: f32) {
        if self.opacity() == opacity {
            return;
        }

        self.opacity.set(opacity);
        self.cast::<Component>().add_dirt(ComponentDirt::RENDER_OPACITY, true);
    }
}

impl ObjectRef<'_, TransformComponent> {
    pub fn render_opacity(&self) -> f32 {
        self.render_opacity.get()
    }

    pub fn world_transform(&self) -> Mat {
        self.world_transform.get()
    }

    pub fn mark_transform_dirty(&self) {
        if !self.cast::<Component>().add_dirt(ComponentDirt::TRANSFORM, false) {
            return;
        }

        self.mark_world_transform_dirty();
    }

    pub fn mark_world_transform_dirty(&self) {
        self.cast::<Component>().add_dirt(ComponentDirt::WORLD_TRANSFORM, true);
    }

    fn x(&self) -> f32 {
        if let Some(root_bone) = self.try_cast::<RootBone>() {
            return root_bone.x();
        }

        if let Some(bone) = self.try_cast::<Bone>() {
            return bone.x();
        }

        if let Some(node) = self.try_cast::<Node>() {
            return node.x();
        }

        unreachable!()
    }

    fn y(&self) -> f32 {
        if let Some(root_bone) = self.try_cast::<RootBone>() {
            return root_bone.y();
        }

        if let Some(bone) = self.try_cast::<Bone>() {
            return bone.y();
        }

        if let Some(node) = self.try_cast::<Node>() {
            return node.y();
        }

        unreachable!()
    }

    pub fn update_transform(&self) {
        let mut transform = if self.rotation() != 0.0 {
            Mat::from_rotation(self.rotation())
        } else {
            Mat::default()
        };

        transform.translate_x = self.x();
        transform.translate_y = self.y();

        transform = transform.scale(math::Vec::new(self.scale_x(), self.scale_y()));

        self.transform.set(transform);
    }

    pub fn update_world_transform(&self) {
        if let Some(parent_transform_component) = self.parent_transform_component.get() {
            let parent_transform = parent_transform_component.as_ref().world_transform.get();
            self.world_transform.set(parent_transform * self.transform.get());
        } else {
            self.world_transform.set(self.transform.get());
        }
    }

    pub fn build_dependencies(&self) {
        let component = self.cast::<Component>();
        if let Some(parent) = component.parent() {
            parent.cast::<Component>().as_ref().push_dependent(component.as_object());
        }
    }

    pub fn update(&self, value: ComponentDirt) {
        if Component::value_has_dirt(value, ComponentDirt::TRANSFORM) {
            self.update_transform();
        }

        if Component::value_has_dirt(value, ComponentDirt::WORLD_TRANSFORM) {
            self.update_world_transform();
        }

        if Component::value_has_dirt(value, ComponentDirt::RENDER_OPACITY) {
            self.render_opacity.set(self.opacity());

            if let Some(parent_transform_component) = self.parent_transform_component.get() {
                self.render_opacity.set(
                    self.render_opacity() * parent_transform_component.as_ref().render_opacity(),
                );
            }
        }
    }
}

impl Core for TransformComponent {
    parent_types![(container_component, ContainerComponent)];

    properties![
        (15, rotation, set_rotation),
        (16, scale_x, set_scale_x),
        (17, scale_y, set_scale_y),
        (18, opacity, set_opacity),
        container_component,
    ];
}

impl OnAdded for ObjectRef<'_, TransformComponent> {
    on_added!([on_added_dirty, import], ContainerComponent);

    fn on_added_clean(&self, _context: &dyn CoreContext) -> StatusCode {
        let parent = self.cast::<Component>().parent().and_then(|parent| parent.try_cast());
        self.parent_transform_component.set(parent);

        StatusCode::Ok
    }
}

impl Default for TransformComponent {
    fn default() -> Self {
        Self {
            container_component: ContainerComponent::default(),
            rotation: Property::new(0.0),
            scale_x: Property::new(1.0),
            scale_y: Property::new(1.0),
            opacity: Property::new(1.0),
            transform: Cell::new(Mat::default()),
            world_transform: Cell::new(Mat::default()),
            render_opacity: Cell::new(0.0),
            parent_transform_component: OptionCell::new(),
        }
    }
}
