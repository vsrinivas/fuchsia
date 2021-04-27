// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::TransitionCondition,
    core::{Core, ObjectRef, OnAdded},
};

#[derive(Debug, Default)]
pub struct TransitionTriggerCondition {
    transition_condition: TransitionCondition,
}

impl Core for TransitionTriggerCondition {
    parent_types![(transition_condition, TransitionCondition)];

    properties!(transition_condition);
}

impl OnAdded for ObjectRef<'_, TransitionTriggerCondition> {
    on_added!(TransitionCondition);
}
