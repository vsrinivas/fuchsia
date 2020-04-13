// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::message::message_client::MessageClient as BaseMessageClient;
use crate::message::message_hub::MessageHub;
use crate::message::messenger::{
    MessengerClient as BaseMessengerClient, MessengerFactory as BaseFactory,
};
use crate::message::receptor::Receptor as BaseReceptor;
use crate::registry::base::Command;
use crate::switchboard::base::{SettingResponseResult, SettingType};
use std::fmt::Debug;

/// This mod defines the common definitions for a MessageHub between the
/// Switchboard and Registry.
pub type MessengerFactory = BaseFactory<Payload, Address>;
pub type MessengerClient = BaseMessengerClient<Payload, Address>;
pub type Receptor = BaseReceptor<Payload, Address>;
pub type MessageClient = BaseMessageClient<Payload, Address>;

// Registry addresses senders by the type they service.
#[derive(PartialEq, Clone, Debug, Eq, Hash)]
pub enum Address {
    Registry,
    Handler(usize),
}

// The types of data that can be sent.
#[derive(Clone, Debug)]
pub enum Payload {
    Command(Command),
    Changed(SettingType),
    Result(SettingResponseResult),
}

pub fn create_message_hub() -> MessengerFactory {
    MessageHub::<Payload, Address>::create()
}

pub fn reply(client: MessageClient, result: SettingResponseResult) {
    client.reply(Payload::Result(result)).send().ack();
}
