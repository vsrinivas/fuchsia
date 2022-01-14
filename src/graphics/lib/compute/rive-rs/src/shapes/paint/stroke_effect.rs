// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{fmt, rc::Rc};

use crate::{
    core::ObjectRef,
    shapes::{paint::TrimPath, CommandPath, MetricsPath},
};

pub fn as_ref(object_ref: ObjectRef<'_>) -> impl StrokeEffect + '_ {
    object_ref.cast::<TrimPath>()
}

pub trait StrokeEffect: fmt::Debug {
    fn effect_path(&self, source: &mut MetricsPath) -> Rc<CommandPath>;
    fn invalidate_effect(&self);
}
