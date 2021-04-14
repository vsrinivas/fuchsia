// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::core::{Core, ObjectRef, OnAdded};

#[derive(Debug, Default)]
pub struct Backboard;

impl Core for Backboard {}

impl OnAdded for ObjectRef<'_, Backboard> {
    on_added!();
}
