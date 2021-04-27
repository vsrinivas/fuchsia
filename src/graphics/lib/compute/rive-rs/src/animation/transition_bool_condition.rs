// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::TransitionValueCondition,
    core::{Core, ObjectRef, OnAdded},
};

#[derive(Debug, Default)]
pub struct TransitionBoolCondition {
    transition_value_condition: TransitionValueCondition,
}

impl Core for TransitionBoolCondition {
    parent_types![(transition_value_condition, TransitionValueCondition)];

    properties!(transition_value_condition);
}

impl OnAdded for ObjectRef<'_, TransitionBoolCondition> {
    on_added!(TransitionValueCondition);
}
