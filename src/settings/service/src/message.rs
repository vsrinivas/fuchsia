// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

pub(self) mod beacon;

/// Common message-related definitions.
pub mod action_fuse;
pub mod base;
pub mod delegate;
pub mod message_builder;
pub mod message_client;
pub mod message_hub;
pub mod messenger;
pub mod receptor;

/// Representation of time used for logging.
pub type Timestamp = zx::Time;

pub trait MessageHubDefinition {
    type Payload: base::Payload + 'static;
    type Address: base::Address + 'static;
    type Role: base::Role + 'static;
}

pub trait MessageHubUtil {
    type Delegate;
    type Audience;
    type Messenger;
    type TargetedMessenger;
    type MessageError;
    type MessageEvent;
    type MessageClient;
    type MessengerType;
    type MessageType;
    type Receptor;
    type Signature;

    fn create_hub() -> Self::Delegate;
}

impl<T> MessageHubUtil for T
where
    T: MessageHubDefinition,
{
    type Delegate = delegate::Delegate<T::Payload, T::Address, T::Role>;
    type Audience = base::Audience<T::Address, T::Role>;
    type Messenger = messenger::MessengerClient<T::Payload, T::Address, T::Role>;
    type TargetedMessenger = messenger::TargetedMessengerClient<T::Payload, T::Address, T::Role>;
    type MessageError = base::MessageError<T::Address>;
    type MessageEvent = base::MessageEvent<T::Payload, T::Address, T::Role>;
    type MessageClient = message_client::MessageClient<T::Payload, T::Address, T::Role>;
    type MessengerType = base::MessengerType<T::Payload, T::Address, T::Role>;
    type MessageType = base::MessageType<T::Payload, T::Address, T::Role>;
    type Receptor = receptor::Receptor<T::Payload, T::Address, T::Role>;
    type Signature = base::Signature<T::Address>;

    fn create_hub() -> Self::Delegate {
        message_hub::MessageHub::<T::Payload, T::Address, T::Role>::create(None)
    }
}
