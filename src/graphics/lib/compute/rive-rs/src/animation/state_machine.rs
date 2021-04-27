// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::Animation,
    core::{Core, ObjectRef, OnAdded},
};

#[derive(Debug, Default)]
pub struct StateMachine {
    animation: Animation,
}

impl Core for StateMachine {
    parent_types![(animation, Animation)];

    properties!(animation);
}

impl OnAdded for ObjectRef<'_, StateMachine> {
    on_added!(Animation);
}
