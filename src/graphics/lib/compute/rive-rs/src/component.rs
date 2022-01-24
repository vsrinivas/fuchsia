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
        match_cast!(self, {
            ClippingShape(clipping_shape) => clipping_shape.build_dependencies(),
            Shape(shape) => shape.build_dependencies(),
            PointsPath(points_path) => points_path.build_dependencies(),
            Path(path) => path.build_dependencies(),
            PathComposer(path_composer) => path_composer.build_dependencies(),
            Skin(skin) => skin.build_dependencies(),
            LinearGradient(linear_gradient) => linear_gradient.build_dependencies(),
            TransformComponent(transform_component) => transform_component.build_dependencies(),
        })
    }

    pub fn on_dirty(&self, dirt: ComponentDirt) {
        match_cast!(self, {
            Path(path) => path.on_dirty(dirt),
            Skin(skin) => skin.on_dirty(dirt),
            Artboard(artboard) => artboard.on_dirty(dirt),
        })
    }

    pub fn update(&self, value: ComponentDirt) {
        match_cast!(self, {
            Ellipse(ellipse) => ellipse.update(value),
            Rectangle(rectangle) => rectangle.update(value),
            Triangle(triangle) => triangle.update(value),
            Polygon(polygon) => polygon.update(value),
            ClippingShape(clipping_shape) => clipping_shape.update(value),
            Shape(shape) => shape.update(value),
            PointsPath(points_path) => points_path.update(value),
            Path(path) => path.update(value),
            PathComposer(path_composer) => path_composer.update(value),
            Skin(skin) => skin.update(value),
            LinearGradient(linear_gradient) => linear_gradient.update(value),
            TransformComponent(transform_component) => transform_component.update(value),
            Artboard(artboard) => artboard.update(value),
        })
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
