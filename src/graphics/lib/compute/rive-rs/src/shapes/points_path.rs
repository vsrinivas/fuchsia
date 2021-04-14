// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    bones::{Skin, Skinnable},
    component::Component,
    component_dirt::ComponentDirt,
    core::{Core, Object, ObjectRef, OnAdded, Property},
    math::Mat,
    option_cell::OptionCell,
    shapes::path::Path,
    transform_component::TransformComponent,
};

#[derive(Debug, Default)]
pub struct PointsPath {
    path: Path,
    is_closed: Property<bool>,
    skin: OptionCell<Object<Skin>>,
}

impl ObjectRef<'_, PointsPath> {
    pub fn is_closed(&self) -> bool {
        self.is_closed.get()
    }

    pub fn set_is_closed(&self, is_closed: bool) {
        self.is_closed.set(is_closed);
    }
}

impl ObjectRef<'_, PointsPath> {
    pub fn transform(&self) -> Mat {
        if self.skin.get().is_some() {
            Mat::default()
        } else {
            self.cast::<TransformComponent>().world_transform()
        }
    }

    pub fn mark_path_dirty(&self) {
        if let Some(skin) = self.skin.get() {
            skin.as_ref().cast::<Component>().add_dirt(ComponentDirt::PATH, false);
        }
    }

    pub fn is_path_closed(&self) -> bool {
        self.is_closed()
    }

    pub fn build_dependencies(&self) {
        self.cast::<Path>().build_dependencies();

        if let Some(skin) = self.skin.get() {
            skin.as_ref().cast::<Component>().push_dependent(self.as_object().cast());
        }
    }

    pub fn update(&self, value: ComponentDirt) {
        let path = self.cast::<Path>();

        if Component::value_has_dirt(value, ComponentDirt::PATH) {
            if let Some(skin) = self.skin.get() {
                skin.as_ref().deform(path.vertices());
            }
        }

        path.update(value);
    }
}

impl Skinnable for ObjectRef<'_, PointsPath> {
    fn skin(&self) -> Option<Object<Skin>> {
        self.skin.get()
    }

    fn set_skin(&self, skin: Object<Skin>) {
        self.skin.set(Some(skin));
    }

    fn mark_skin_dirty(&self) {
        self.cast::<Path>().mark_path_dirty();
    }
}

impl Core for PointsPath {
    parent_types![(path, Path)];

    properties![(32, is_closed, set_is_closed), path];
}

impl OnAdded for ObjectRef<'_, PointsPath> {
    on_added!(Path);
}
