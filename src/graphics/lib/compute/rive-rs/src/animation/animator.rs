// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::core::{Core, ObjectRef};

pub(crate) struct Animator<T> {
    val: T,
}

impl<T: Clone> Animator<T> {
    pub fn new(val: T) -> Self {
        Self { val }
    }

    pub fn animate<'a, C: Core, S>(&self, object: &'a ObjectRef<'a, C>, setter: S)
    where
        S: Fn(&'a ObjectRef<'a, C>, T),
    {
        setter(object, self.val.clone());
    }
}
