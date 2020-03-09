// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::message::message_client::MessageClient as BaseMessageClient;
use crate::message::message_hub::{MessageHub, MessageHubHandle as BaseHubHandle};
use crate::message::messenger::Messenger as BaseMessenger;
use crate::switchboard::base::{SettingAction, SettingEvent};

/// This mod defines the common definitions for a MessageHub between the
/// Switchboard and Registry.

pub type MessageHubHandle = BaseHubHandle<Payload, Address>;
pub type Messenger = BaseMessenger<Payload, Address>;
pub type MessageClient = BaseMessageClient<Payload, Address>;

#[derive(PartialEq, Clone, Eq, Hash)]
pub enum Address {
    Switchboard,
    Registry,
}

// The types of data that can be sent.
#[derive(Clone)]
pub enum Payload {
    Action(SettingAction),
    Event(SettingEvent),
}

pub fn create_message_hub() -> MessageHubHandle {
    MessageHub::<Payload, Address>::create()
}
