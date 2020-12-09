// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

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
            use crate::message::base::MessageError as BaseMessageError;
            use crate::message::base::Signature as BaseSignature;
            use crate::message::message_client::MessageClient as BaseMessageClient;
            use crate::message::message_hub::MessageHub;
            use crate::message::messenger::{
                MessengerClient as BaseMessengerClient, MessengerFactory as BaseFactory,
            };
            use crate::message::receptor::Receptor as BaseReceptor;

            pub type Factory = BaseFactory<$payload, $address, $role>;

            #[allow(dead_code)]
            pub type Messenger = BaseMessengerClient<$payload, $address, $role>;

            #[allow(dead_code)]
            pub type MessageError = BaseMessageError<$address>;

            #[allow(dead_code)]
            pub type Client = BaseMessageClient<$payload, $address, $role>;

            #[allow(dead_code)]
            pub type Receptor = BaseReceptor<$payload, $address, $role>;

            #[allow(dead_code)]
            pub type Signature = BaseSignature<$address>;

            pub fn create_hub() -> Factory {
                MessageHub::<$payload, $address, $role>::create(None)
            }
        }
    };
}

/// Representation of time used for logging.
pub type Timestamp = zx::Time;

pub fn now() -> Timestamp {
    zx::Time::get_monotonic()
}
