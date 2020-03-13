// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::message::message_client::MessageClient as BaseMessageClient;
use crate::message::message_hub::MessageHub;
use crate::message::messenger::{
    MessengerClient as BaseMessengerClient, MessengerFactory as BaseFactory,
};
use crate::switchboard::base::{SettingAction, SettingEvent};
use std::fmt::Debug;

/// This mod defines the common definitions for a MessageHub between the
/// Switchboard and Registry.
pub type MessengerFactory = BaseFactory<Payload, Address>;
pub type MessengerClient = BaseMessengerClient<Payload, Address>;
pub type MessageClient = BaseMessageClient<Payload, Address>;

#[derive(PartialEq, Clone, Debug, Eq, Hash)]
pub enum Address {
    Switchboard,
    Registry,
}

// The types of data that can be sent.
#[derive(Clone, Debug)]
pub enum Payload {
    Action(SettingAction),
    Event(SettingEvent),
}

pub fn create_message_hub() -> MessengerFactory {
    MessageHub::<Payload, Address>::create()
}
