// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::{LinearAnimation, StateMachine},
    artboard::Artboard,
    core::Object,
    importers::ImportStackObject,
    status_code::StatusCode,
};

#[derive(Debug)]
pub struct ArtboardImporter {
    artboard: Object<Artboard>,
}

impl ArtboardImporter {
    pub fn new(artboard: Object<Artboard>) -> Self {
        Self { artboard }
    }

    pub fn push_object(&self, object: Object) {
        self.artboard.as_ref().push_object(object);
    }

    pub fn push_animation(&self, animation: Object<LinearAnimation>) {
        self.artboard.as_ref().push_animation(animation);
    }

    pub fn _push_state_machine(&self, state_machine: Object<StateMachine>) {
        self.artboard.as_ref().push_state_machine(state_machine);
    }
}

impl ImportStackObject for ArtboardImporter {
    fn resolve(&self) -> StatusCode {
        self.artboard.as_ref().initialize()
    }
}
