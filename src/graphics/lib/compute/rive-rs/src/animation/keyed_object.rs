// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::any::TypeId;

use crate::{
    animation::{KeyedProperty, LinearAnimation},
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    dyn_vec::DynVec,
    importers::{ImportStack, LinearAnimationImporter},
    status_code::StatusCode,
    Artboard,
};

#[derive(Debug, Default)]
pub struct KeyedObject {
    object_id: Property<u64>,
    keyed_properties: DynVec<Object<KeyedProperty>>,
}

impl ObjectRef<'_, KeyedObject> {
    pub fn object_id(&self) -> u64 {
        self.object_id.get()
    }

    pub fn set_object_id(&self, object_id: u64) {
        self.object_id.set(object_id)
    }
}

impl ObjectRef<'_, KeyedObject> {
    pub fn push_keyed_property(&self, keyed_property: Object<KeyedProperty>) {
        self.keyed_properties.push(keyed_property);
    }

    pub fn apply(&self, artboard: Object<Artboard>, time: f32, mix: f32) {
        if let Some(core) = artboard.as_ref().resolve(self.object_id() as usize) {
            for property in self.keyed_properties.iter() {
                property.as_ref().apply(core.clone(), time, mix);
            }
        }
    }
}

impl Core for KeyedObject {
    properties![(51, object_id, set_object_id)];
}

impl OnAdded for ObjectRef<'_, KeyedObject> {
    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        if context.resolve(self.object_id() as usize).is_none() {
            return StatusCode::MissingObject;
        }

        for property in self.keyed_properties.iter() {
            property.as_ref().on_added_dirty(context);
        }

        StatusCode::Ok
    }

    fn on_added_clean(&self, context: &dyn CoreContext) -> StatusCode {
        for property in self.keyed_properties.iter() {
            property.as_ref().on_added_clean(context);
        }

        StatusCode::Ok
    }

    fn import(&self, object: Object, import_stack: &ImportStack) -> StatusCode {
        if let Some(importer) =
            import_stack.latest::<LinearAnimationImporter>(TypeId::of::<LinearAnimation>())
        {
            importer.push_keyed_object(object.as_ref().cast::<KeyedObject>().as_object());
            StatusCode::Ok
        } else {
            StatusCode::MissingObject
        }
    }
}
