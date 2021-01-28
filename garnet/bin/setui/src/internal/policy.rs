// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::message_hub_definition;
use crate::policy::base::{response::Response, Request};
use std::fmt::Debug;

#[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
pub enum Address {
    Policy(SettingType),
}

#[derive(PartialEq, Clone, Debug)]
pub enum Payload {
    Request(Request),
    Response(Response),
}

/// `Role` defines grouping for responsibilities on the policy message hub.
#[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
pub enum Role {
    /// This role indicates that the messenger handles and enacts policy requests.
    PolicyHandler,
}

message_hub_definition!(Payload, Address, Role);
