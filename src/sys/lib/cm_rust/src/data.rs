// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_sys2 as fsys;

pub trait ObjectExt {
    fn find(&self, key: &str) -> Option<&fsys::Value>;
}

impl ObjectExt for fsys::Object {
    fn find(&self, key: &str) -> Option<&fsys::Value> {
        for entry in self.entries.iter() {
            if entry.key == key {
                return entry.value.as_ref().map(|x| &**x);
            }
        }
        None
    }
}
