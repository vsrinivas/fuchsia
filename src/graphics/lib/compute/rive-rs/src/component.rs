// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    any::TypeId,
    cell::Cell,
    hash::{Hash, Hasher},
    iter,
};

use crate::{
    artboard::Artboard,
    bones::Skin,
    component_dirt::ComponentDirt,
    container_component::ContainerComponent,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    dyn_vec::DynVec,
    importers::{ArtboardImporter, ImportStack},
    option_cell::OptionCell,
    shapes::{
        paint::LinearGradient, ClippingShape, Ellipse, Path, PathComposer, PointsPath, Polygon,
        Rectangle, Shape, Triangle,
    },
    status_code::StatusCode,
    TransformComponent,
};

#[derive(Debug)]
pub struct Component {
    name: Property<String>,
    parent_id: Property<u64>,
    parent: OptionCell<Object<ContainerComponent>>,
    depdenents: DynVec<Object<Self>>,
    artboard: OptionCell<Object<Artboard>>,
    pub(crate) graph_order: Cell<usize>,
    pub(crate) dirt: Cell<ComponentDirt>,
}

impl<'a> ObjectRef<'a, Component> {
    pub fn name(&self) -> String {
        self.name.get()
    }

    pub fn set_name(&self, name: String) {
        self.name.set(name);
    }

    pub fn parent_id(&'a self) -> u64 {
        self.parent_id.get()
    }

    pub fn set_parent_id(&self, parent_id: u64) {
        self.parent_id.set(parent_id);
    }
}

impl Component {
    pub fn artboard(&self) -> Option<Object<Artboard>> {
        self.artboard.get()
    }

    pub fn value_has_dirt(value: ComponentDirt, flags: ComponentDirt) -> bool {
        !(value & flags).is_empty()
    }
}

impl ObjectRef<'_, Component> {
    pub fn parent(&self) -> Option<Object<ContainerComponent>> {
        self.parent.get()
    }

    pub fn parents(&self) -> impl Iterator<Item = Object<ContainerComponent>> {
        iter::successors(self.parent(), |component| component.cast::<Component>().as_ref().parent())
    }

    pub fn depdenents(&self) -> impl Iterator<Item = Object<Component>> + '_ {
        self.depdenents.iter()
    }

    pub fn push_dependent(&self, dependent: Object<Component>) {
        self.depdenents.push(dependent);
    }

    pub fn has_dirt(&self, flags: ComponentDirt) -> bool {
        self.dirt.get() & flags == flags
    }

    pub fn add_dirt(&self, value: ComponentDirt, recurse: bool) -> bool {
        if self.has_dirt(value) {
            return false;
        }

        self.dirt.set(self.dirt.get() | value);
        self.on_dirty(self.dirt.get());

        if let Some(artboard) = self.artboard.get() {
            artboard.as_ref().on_component_dirty(self);
        }

        if !recurse {
            return true;
        }

        for dependent in self.depdenents() {
            dependent.as_ref().add_dirt(value, recurse);
        }

        true
    }

    pub fn build_dependencies(&self) {
        if let Some(clipping_shape) = self.try_cast::<ClippingShape>() {
            return clipping_shape.build_dependencies();
        }

        if let Some(shape) = self.try_cast::<Shape>() {
            return shape.build_dependencies();
        }

        if let Some(points_path) = self.try_cast::<PointsPath>() {
            return points_path.build_dependencies();
        }

        if let Some(path) = self.try_cast::<Path>() {
            return path.build_dependencies();
        }

        if let Some(path_composer) = self.try_cast::<PathComposer>() {
            return path_composer.build_dependencies();
        }

        if let Some(skin) = self.try_cast::<Skin>() {
            return skin.build_dependencies();
        }

        if let Some(linear_gradient) = self.try_cast::<LinearGradient>() {
            return linear_gradient.build_dependencies();
        }

        if let Some(transform_component) = self.try_cast::<TransformComponent>() {
            return transform_component.build_dependencies();
        }
    }

    pub fn on_dirty(&self, dirt: ComponentDirt) {
        if let Some(path) = self.try_cast::<Path>() {
            return path.on_dirty(dirt);
        }

        if let Some(skin) = self.try_cast::<Skin>() {
            return skin.on_dirty(dirt);
        }

        if let Some(artboard) = self.try_cast::<Artboard>() {
            return artboard.on_dirty(dirt);
        }
    }

    pub fn update(&self, value: ComponentDirt) {
        if let Some(ellipse) = self.try_cast::<Ellipse>() {
            return ellipse.update(value);
        }

        if let Some(rectangle) = self.try_cast::<Rectangle>() {
            return rectangle.update(value);
        }

        if let Some(triangle) = self.try_cast::<Triangle>() {
            return triangle.update(value);
        }

        if let Some(polygon) = self.try_cast::<Polygon>() {
            return polygon.update(value);
        }

        if let Some(clipping_shape) = self.try_cast::<ClippingShape>() {
            return clipping_shape.update(value);
        }

        if let Some(shape) = self.try_cast::<Shape>() {
            return shape.update(value);
        }

        if let Some(points_path) = self.try_cast::<PointsPath>() {
            return points_path.update(value);
        }

        if let Some(path) = self.try_cast::<Path>() {
            return path.update(value);
        }

        if let Some(path_composer) = self.try_cast::<PathComposer>() {
            return path_composer.update(value);
        }

        if let Some(skin) = self.try_cast::<Skin>() {
            return skin.update(value);
        }

        if let Some(linear_gradient) = self.try_cast::<LinearGradient>() {
            return linear_gradient.update(value);
        }

        if let Some(transform_component) = self.try_cast::<TransformComponent>() {
            return transform_component.update(value);
        }

        if let Some(artboard) = self.try_cast::<Artboard>() {
            return artboard.update(value);
        }
    }
}

impl Hash for Component {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.name.get().hash(state);
        self.parent_id.get().hash(state);
    }
}

impl Core for Component {
    properties![(4, name, set_name), (5, parent_id, set_parent_id)];
}

impl OnAdded for ObjectRef<'_, Component> {
    on_added!([on_added_clean]);

    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        let artboard = context.artboard();
        self.artboard.set(Some(artboard.clone()));

        if let Some(self_artboard) = self.as_object().try_cast() {
            if artboard == self_artboard {
                return StatusCode::Ok;
            }
        }

        if let Some(parent) =
            context.resolve(self.parent_id() as usize).and_then(|core| core.try_cast())
        {
            self.parent.set(Some(parent));
            StatusCode::Ok
        } else {
            StatusCode::MissingObject
        }
    }

    fn import(&self, object: Object, import_stack: &ImportStack) -> StatusCode {
        if let Some(importer) = import_stack.latest::<ArtboardImporter>(TypeId::of::<Artboard>()) {
            importer.push_object(object);
            StatusCode::Ok
        } else {
            StatusCode::MissingObject
        }
    }
}

impl Default for Component {
    fn default() -> Self {
        Self {
            name: Property::new(String::new()),
            parent_id: Property::new(0),
            parent: OptionCell::new(),
            depdenents: DynVec::new(),
            artboard: OptionCell::new(),
            graph_order: Cell::new(0),
            dirt: Cell::new(ComponentDirt::all()),
        }
    }
}
