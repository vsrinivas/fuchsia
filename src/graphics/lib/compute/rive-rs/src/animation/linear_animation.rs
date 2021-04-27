// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::any::TypeId;

use crate::{
    animation::{Animation, KeyedObject, Loop},
    artboard::Artboard,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    dyn_vec::DynVec,
    importers::{ArtboardImporter, ImportStack},
    status_code::StatusCode,
};

#[derive(Debug)]
pub struct LinearAnimation {
    animation: Animation,
    fps: Property<u64>,
    duration: Property<u64>,
    speed: Property<f32>,
    r#loop: Property<Loop>,
    work_start: Property<u64>,
    work_end: Property<u64>,
    enable_work_area: Property<bool>,
    keyed_objects: DynVec<Object<KeyedObject>>,
}

impl ObjectRef<'_, LinearAnimation> {
    pub fn fps(&self) -> u64 {
        self.fps.get()
    }

    pub fn set_fps(&self, fps: u64) {
        self.fps.set(fps);
    }

    pub fn duration(&self) -> u64 {
        self.duration.get()
    }

    pub fn set_duration(&self, duration: u64) {
        self.duration.set(duration);
    }

    pub fn speed(&self) -> f32 {
        self.speed.get()
    }

    pub fn set_speed(&self, speed: f32) {
        self.speed.set(speed);
    }

    pub fn r#loop(&self) -> Loop {
        self.r#loop.get()
    }

    pub fn set_loop(&self, r#loop: Loop) {
        self.r#loop.set(r#loop);
    }

    pub fn work_start(&self) -> u64 {
        self.work_start.get()
    }

    pub fn set_work_start(&self, work_start: u64) {
        self.work_start.set(work_start);
    }

    pub fn work_end(&self) -> u64 {
        self.work_end.get()
    }

    pub fn set_work_end(&self, work_end: u64) {
        self.work_end.set(work_end);
    }

    pub fn enable_work_area(&self) -> bool {
        self.enable_work_area.get()
    }

    pub fn set_enable_work_area(&self, enable_work_area: bool) {
        self.enable_work_area.set(enable_work_area);
    }
}

impl ObjectRef<'_, LinearAnimation> {
    pub fn push_keyed_object(&self, keyed_object: Object<KeyedObject>) {
        self.keyed_objects.push(keyed_object);
    }

    pub fn apply(&self, artboard: Object<Artboard>, time: f32, mix: f32) {
        for object in self.keyed_objects.iter() {
            object.as_ref().apply(artboard.clone(), time, mix);
        }
    }
}

impl Core for LinearAnimation {
    parent_types![(animation, Animation)];

    properties![
        (56, fps, set_fps),
        (57, duration, set_duration),
        (58, speed, set_speed),
        (59, r#loop, set_loop),
        (60, work_start, set_work_start),
        (61, work_end, set_work_end),
        (62, enable_work_area, set_enable_work_area),
        animation,
    ];
}

impl OnAdded for ObjectRef<'_, LinearAnimation> {
    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        for object in self.keyed_objects.iter() {
            let code = object.as_ref().on_added_dirty(context);
            if code != StatusCode::Ok {
                return code;
            }
        }

        StatusCode::Ok
    }

    fn on_added_clean(&self, context: &dyn CoreContext) -> StatusCode {
        for object in self.keyed_objects.iter() {
            let code = object.as_ref().on_added_dirty(context);
            if code != StatusCode::Ok {
                return code;
            }
        }

        StatusCode::Ok
    }

    fn import(&self, object: Object, import_stack: &ImportStack) -> StatusCode {
        if let Some(importer) = import_stack.latest::<ArtboardImporter>(TypeId::of::<Artboard>()) {
            importer.push_animation(object.as_ref().cast::<LinearAnimation>().as_object());
            self.cast::<Animation>().import(object, import_stack)
        } else {
            StatusCode::MissingObject
        }
    }
}

impl Default for LinearAnimation {
    fn default() -> Self {
        Self {
            animation: Animation::default(),
            fps: Property::new(60),
            duration: Property::new(60),
            speed: Property::new(1.0),
            r#loop: Property::new(Loop::default()),
            work_start: Property::new(0),
            work_end: Property::new(0),
            enable_work_area: Property::new(false),
            keyed_objects: DynVec::new(),
        }
    }
}
