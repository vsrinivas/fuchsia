// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use crate::{
    assert_inspect_tree,
    testing::{
        macros::{AnyProperty, NodeHierarchyGetter, PropertyAssertion, TreeAssertion},
        reader::{ComponentSelector, InspectDataFetcher},
    },
};

#[macro_use]
mod macros;
mod reader;
