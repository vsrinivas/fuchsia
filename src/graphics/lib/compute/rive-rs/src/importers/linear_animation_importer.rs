// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::{KeyedObject, LinearAnimation},
    core::Object,
    importers::ImportStackObject,
};

#[derive(Debug)]
pub struct LinearAnimationImporter {
    animation: Object<LinearAnimation>,
}

impl LinearAnimationImporter {
    pub fn new(animation: Object<LinearAnimation>) -> Self {
        Self { animation }
    }

    pub fn animation(&self) -> Object<LinearAnimation> {
        self.animation.clone()
    }

    pub fn push_keyed_object(&self, keyed_object: Object<KeyedObject>) {
        self.animation.as_ref().push_keyed_object(keyed_object);
    }
}

impl ImportStackObject for LinearAnimationImporter {}
