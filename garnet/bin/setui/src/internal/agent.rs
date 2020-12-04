// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::agent::base::{Invocation, InvocationResult};

#[derive(Clone, Debug)]
pub enum Payload {
    Invocation(Invocation),
    Complete(InvocationResult),
}

crate::message_hub_definition!(Payload);
