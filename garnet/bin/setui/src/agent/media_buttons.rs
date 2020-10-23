// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::base::{Context as AgentContext, Descriptor};
use crate::blueprint_definition;

blueprint_definition!(Descriptor::Component("buttons_agent"), MediaButtonsAgent::create);

pub struct MediaButtonsAgent;

impl MediaButtonsAgent {
    async fn create(_context: AgentContext) {
        // TODO(fxb/57917) Handle media buttons
    }
}
