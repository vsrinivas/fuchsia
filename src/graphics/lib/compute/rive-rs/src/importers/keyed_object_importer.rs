// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::{KeyedObject, KeyedProperty},
    core::Object,
    importers::ImportStackObject,
};

#[derive(Debug)]
pub struct KeyedObjectImporter {
    keyed_object: Object<KeyedObject>,
}

impl KeyedObjectImporter {
    pub fn new(keyed_object: Object<KeyedObject>) -> Self {
        Self { keyed_object }
    }

    pub fn push_keyed_property(&self, keyed_property: Object<KeyedProperty>) {
        self.keyed_object.as_ref().push_keyed_property(keyed_property);
    }
}

impl ImportStackObject for KeyedObjectImporter {}
