// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;

use crate::{
    bones::Skin,
    core::{Object, ObjectRef},
    shapes::PointsPath,
};

pub fn try_from(core: Object) -> Option<Object> {
    core.try_cast::<PointsPath>().map(|object| object.into())
}

pub fn as_ref(object_ref: ObjectRef<'_>) -> impl Skinnable + '_ {
    object_ref.cast::<PointsPath>()
}

pub trait Skinnable: fmt::Debug {
    fn skin(&self) -> Option<Object<Skin>>;
    fn set_skin(&self, skin: Object<Skin>);
    fn mark_skin_dirty(&self);
}
