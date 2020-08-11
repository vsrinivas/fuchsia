// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message_hub_definition;
use crate::registry::base::{Command, SettingHandlerResult};
use crate::switchboard::base::SettingType;
use std::fmt::Debug;

// Registry addresses senders by the type they service.
#[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
pub enum Address {
    Handler(usize),
}

// The types of data that can be sent.
#[derive(Clone, Debug, PartialEq)]
pub enum Payload {
    Command(Command),
    Changed(SettingType),
    Result(SettingHandlerResult),
}

pub fn reply(client: message::Client, result: SettingHandlerResult) {
    client.reply(Payload::Result(result)).send().ack();
}

message_hub_definition!(Payload, Address);
