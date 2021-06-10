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

/// Macro for defining a standard message hub
#[macro_export]
macro_rules! message_hub_definition {
    ($payload:ty) => {
        crate::message_hub_definition!($payload, $crate::message::base::default::Address);
    };
    ($payload:ty, $address:ty) => {
        crate::message_hub_definition!($payload, $address, $crate::message::base::default::Role);
    };
    ($payload:ty, $address:ty, $role:ty) => {
        pub mod message {
            #[allow(unused_imports)]
            use super::*;
            use crate::message::base::Audience as BaseAudience;
            use crate::message::base::MessageError as BaseMessageError;
            use crate::message::base::MessageEvent as BaseMessageEvent;
            use crate::message::base::MessageType as BaseMessageType;
            use crate::message::base::MessengerType as BaseMessengerType;
            use crate::message::base::Signature as BaseSignature;
            use crate::message::delegate::Delegate as BaseDelegate;
            use crate::message::message_client::MessageClient as BaseMessageClient;
            use crate::message::message_hub::MessageHub;
            use crate::message::messenger::{
                MessengerClient as BaseMessengerClient,
                TargetedMessengerClient as BaseTargetedMessengerClient,
            };
            use crate::message::receptor::Receptor as BaseReceptor;

            pub(crate) type Delegate = BaseDelegate<$payload, $address, $role>;

            #[allow(dead_code)]
            pub(crate) type Audience = BaseAudience<$address, $role>;

            #[allow(dead_code)]
            pub(crate) type Messenger = BaseMessengerClient<$payload, $address, $role>;

            #[allow(dead_code)]
            pub(crate) type TargetedMessenger =
                BaseTargetedMessengerClient<$payload, $address, $role>;

            #[allow(dead_code)]
            pub(crate) type MessageError = BaseMessageError<$address>;

            #[allow(dead_code)]
            pub(crate) type MessageEvent = BaseMessageEvent<$payload, $address, $role>;

            #[allow(dead_code)]
            pub(crate) type MessageClient = BaseMessageClient<$payload, $address, $role>;

            #[allow(dead_code)]
            pub(crate) type MessengerType = BaseMessengerType<$payload, $address, $role>;

            #[allow(dead_code)]
            pub(crate) type MessageType = BaseMessageType<$payload, $address, $role>;

            #[allow(dead_code)]
            pub(crate) type Receptor = BaseReceptor<$payload, $address, $role>;

            #[allow(dead_code)]
            pub(crate) type Signature = BaseSignature<$address>;

            #[allow(dead_code)]
            pub(crate) fn create_hub() -> Delegate {
                MessageHub::<$payload, $address, $role>::create(None)
            }
        }
    };
}
