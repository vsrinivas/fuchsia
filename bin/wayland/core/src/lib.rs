// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use failure::Fail;
use std::io;

mod message;
pub use crate::message::*;
mod object;
pub use crate::object::*;
mod registry;
pub use crate::registry::*;
#[cfg(test)]
mod test_protocol;

pub type ObjectId = u32;
pub type NewId = u32;

/// Trait to be implemented by any type used as an interface 'event'.
pub trait IntoMessage: Sized {
    type Error: Fail;
    /// Consumes |self| and serializes into a |Message|.
    fn into_message(self, id: u32) -> Result<Message, Self::Error>;
}

/// Trait to be implemented by any type used as an interface 'request'.
pub trait FromMessage: Sized {
    type Error: Fail;
    /// Consumes |msg| creates an instance of self.
    fn from_message(msg: Message) -> Result<Self, Self::Error>;
}

/// An array of |ArgKind|s for a single request or event message.
pub struct MessageSpec(pub &'static [ArgKind]);

/// An array of |MessageSpec|s for either a set of requests or events.
///
/// The slice is indexed by message opcode.
pub struct MessageGroupSpec(pub &'static [MessageSpec]);

pub trait Interface {
    /// The name of this interface. This will correspond to the 'name' attribute
    /// on the 'interface' element in the wayland protocol XML.
    const NAME: &'static str;

    /// The version of this interface. This will correspond to the 'version'
    /// attribute on the 'interface' element in the wayland protocol XML.
    const VERSION: u32;

    /// A description of the structure of request messages.
    const REQUESTS: MessageGroupSpec;

    /// A description of the structure of event messages.
    const EVENTS: MessageGroupSpec;

    /// The rust type that can hold the decoded request messages.
    type Request: FromMessage;

    /// The rust type that can hold the decoded event messages.
    type Event: IntoMessage;
}

#[derive(Debug, Fail)]
pub enum DecodeError {
    #[fail(display = "invalid message opcode: {}", _0)]
    InvalidOpcode(u16),
    #[fail(display = "{}", _0)]
    IoError(#[cause] io::Error),
}

impl From<io::Error> for DecodeError {
    fn from(e: io::Error) -> Self {
        DecodeError::IoError(e)
    }
}

#[derive(Debug, Fail)]
pub enum EncodeError {
    #[fail(display = "{}", _0)]
    IoError(#[cause] io::Error),
}

impl From<io::Error> for EncodeError {
    fn from(e: io::Error) -> Self {
        EncodeError::IoError(e)
    }
}
