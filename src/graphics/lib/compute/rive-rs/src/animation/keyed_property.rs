// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::any::TypeId;

use crate::{
    animation::{KeyFrame, KeyedObject},
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    dyn_vec::DynVec,
    importers::{ImportStack, KeyedObjectImporter},
    status_code::StatusCode,
};

#[derive(Debug, Default)]
pub struct KeyedProperty {
    property_key: Property<u64>,
    key_frames: DynVec<Object<KeyFrame>>,
}

impl ObjectRef<'_, KeyedProperty> {
    pub fn property_key(&self) -> u64 {
        self.property_key.get()
    }

    pub fn set_property_key(&self, property_key: u64) {
        self.property_key.set(property_key)
    }
}

impl ObjectRef<'_, KeyedProperty> {
    pub fn push_key_frame(&self, key_frame: Object<KeyFrame>) {
        self.key_frames.push(key_frame);
    }

    pub fn apply(&self, core: Object, seconds: f32, mix: f32) {
        assert!(!self.key_frames.is_empty());

        let mut i = 0;
        let mut mid;
        let mut closest_seconds;
        let mut start = 0;
        let mut end = self.key_frames.len() - 1;

        while start <= end {
            mid = (start + end) / 2;
            closest_seconds = self.key_frames.index(mid).as_ref().seconds();

            if closest_seconds < seconds {
                start = mid + 1;
            } else if closest_seconds > seconds {
                if let Some(new_end) = mid.checked_sub(1) {
                    end = new_end;
                } else {
                    break;
                }
            } else {
                i = mid;
                break;
            }

            i = start;
        }

        let property_key = self.property_key();

        if i == 0 {
            self.key_frames.index(0).as_ref().apply(core, property_key, mix);
            return;
        }

        if i == self.key_frames.len() {
            self.key_frames.index(i - 1).as_ref().apply(core, property_key, mix);
            return;
        }

        let from_frame = self.key_frames.index(i - 1);
        let from_frame = from_frame.as_ref();
        let to_frame = self.key_frames.index(i);
        let to_frame = to_frame.as_ref();

        if seconds == to_frame.seconds() {
            to_frame.apply(core, property_key, mix);
        } else if from_frame.interpolation_type() == 0 {
            from_frame.apply(core, property_key, mix);
        } else {
            from_frame.apply_interpolation(core, property_key, seconds, to_frame, mix);
        }
    }
}

impl Core for KeyedProperty {
    properties![(53, property_key, set_property_key)];
}

impl OnAdded for ObjectRef<'_, KeyedProperty> {
    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        for key_frame in self.key_frames.iter() {
            let code = key_frame.as_ref().on_added_dirty(context);
            if code != StatusCode::Ok {
                return code;
            }
        }

        StatusCode::Ok
    }

    fn on_added_clean(&self, context: &dyn CoreContext) -> StatusCode {
        for key_frame in self.key_frames.iter() {
            let code = key_frame.as_ref().on_added_clean(context);
            if code != StatusCode::Ok {
                return code;
            }
        }

        StatusCode::Ok
    }

    fn import(&self, object: Object, import_stack: &ImportStack) -> StatusCode {
        if let Some(importer) =
            import_stack.latest::<KeyedObjectImporter>(TypeId::of::<KeyedObject>())
        {
            importer.push_keyed_property(object.as_ref().cast::<KeyedProperty>().as_object());
            StatusCode::Ok
        } else {
            StatusCode::MissingObject
        }
    }
}
