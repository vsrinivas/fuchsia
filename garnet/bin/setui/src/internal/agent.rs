// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::agent::base::{Invocation, InvocationResult};
use crate::message::message_hub::MessageHub;
use crate::message::messenger::{
    MessengerClient as BaseMessengerClient, MessengerFactory as BaseFactory,
};
use crate::message::receptor::Receptor as BaseReceptor;

pub type MessengerFactory = BaseFactory<Payload, Address>;
pub type MessengerClient = BaseMessengerClient<Payload, Address>;
pub type Receptor = BaseReceptor<Payload, Address>;

#[derive(PartialEq, Clone, Debug, Eq, Hash)]
pub enum Address {}

#[derive(Clone, Debug)]
pub enum Payload {
    Invocation(Invocation),
    Complete(InvocationResult),
}

pub fn create_message_hub() -> MessengerFactory {
    MessageHub::<Payload, Address>::create()
}
