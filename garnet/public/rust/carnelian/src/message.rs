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
