// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This test crate demonstrates that `fuchsia_inspect_derive` can be used
//! standalone, without depending on `fuchsia_inspect`. This is
//! important since the procedural macros must output code that uses types that
//! must be locally available in the namespace of the user code. Concretely,
//! this would occur if the user depends on crate which re-exports
//! `fuchsia_inspect` types, instead of depending on `fuchsia_inspect`
//! directly.

#![cfg(test)]

use fuchsia_inspect_derive::{AttachError, IValue, Inspect, Unit, WithInspect};

// Import from `fuchsia_inspect_derive`, since we don't have access to
// `fuchsia_inspect`.
use fuchsia_inspect_derive::InspectNode;

#[derive(Inspect, Default)]
struct Yak {
    #[inspect(rename = "hair_length_mm")]
    child: IValue<Yakling>,
    inspect_node: InspectNode,
}

#[derive(Unit, Default)]
struct Yakling {
    age: u16,
}

#[test]
fn initialize() -> Result<(), AttachError> {
    let parent = InspectNode::default();
    Yak::default().with_inspect(&parent, "my_yak").map(|_| ())
}
