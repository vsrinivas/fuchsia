// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use crate::{container::ComponentIdentity, events::types::ComponentIdentifier};
use lazy_static::lazy_static;
use std::sync::Arc;

lazy_static! {
    pub static ref TEST_IDENTITY: Arc<ComponentIdentity> = {
        Arc::new(ComponentIdentity::from_identifier_and_url(
            &ComponentIdentifier::Legacy {
                moniker: vec!["fake-test-env", "test-component.cmx"].into(),
                instance_id: "".into(),
            },
            "fuchsia-pkg://fuchsia.com/testing123#test-component.cmx",
        ))
    };
}
