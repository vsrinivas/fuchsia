// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// TODO(fxb/59747): remove dead_code macro once used in production code as well.
#![allow(dead_code)]

use crate::message_hub_definition;
use crate::policy::base::{response::Response, Request};
use crate::switchboard::base::SettingType;
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

message_hub_definition!(Payload, Address);
