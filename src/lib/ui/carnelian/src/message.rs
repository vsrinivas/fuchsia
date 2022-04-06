// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::any::Any;

/// Message type
pub type Message = Box<dyn Any>;

/// Make a message
pub fn make_message<A: Any>(message_contents: A) -> Message {
    Box::new(message_contents)
}

/// Macro to implement a handle message trait method that delegates
/// known types to other methods on the struct implementing ViewAssistant.
#[macro_export]
macro_rules! derive_handle_message {
    (
        $(
            $message_type:ty => $handler_method:ident
        ),
        +
    ) => {
        fn handle_message(&mut self, msg: carnelian::Message) {
            $(
            if let Some(my_message) = msg.downcast_ref::<$message_type>() {
                self.$handler_method(my_message);
                return;
            }
            )*
        }
    };
}

/// Macro to implement a handle message trait method that delegates
/// known types to other methods on the struct implementing ViewAssistant.
#[macro_export]
macro_rules! derive_handle_message_with_default {
    (
        $default_handler_method:ident,
        $(
            $message_type:ty => $handler_method:ident
        ),
        +
    ) => {
        fn handle_message(&mut self, msg: carnelian::Message) {
            $(
            if let Some(my_message) = msg.downcast_ref::<$message_type>() {
                self.$handler_method(my_message);
                return;
            }
            )*
            self.$default_handler_method(&msg);
        }
    };
}
