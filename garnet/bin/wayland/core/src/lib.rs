// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, Fail};
use std::fmt::{self, Debug, Display};
use std::io;
use std::marker::PhantomData;

mod fixed;
pub use crate::fixed::*;
mod message;
pub use crate::message::*;

pub type ObjectId = u32;
pub type NewId = u32;

/// Common base trait for all rust types that model wayland Requests or Events.
pub trait MessageType {
    /// Generates a string suitable for protocol logging this message.
    fn log(&self, this: ObjectId) -> String;

    /// Returns a static CStr reference that describes the interface/method of
    /// this message.
    ///
    /// Ex: 'wl_interface::method_name'
    fn message_name(&self) -> &'static std::ffi::CStr;
}

/// Trait to be implemented by any type used as an interface 'event'.
pub trait IntoMessage: Sized + MessageType {
    type Error: Fail;
    /// Consumes |self| and serializes into a |Message|.
    fn into_message(self, id: u32) -> Result<Message, Self::Error>;
}

/// Trait to be implemented by any type used as an interface 'request'.
pub trait FromArgs: Sized + MessageType {
    /// Consumes |args| creates an instance of self.
    fn from_args(op: u16, args: Vec<Arg>) -> Result<Self, Error>;
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
    type Request: FromArgs;

    /// The rust type that can hold the decoded event messages.
    type Event: IntoMessage;
}

#[derive(Debug, Fail)]
pub enum DecodeError {
    #[fail(display = "invalid message opcode: {}", _0)]
    InvalidOpcode(u16),
    #[fail(display = "end of argument list occurred while decoding")]
    InsufficientArgs,
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

#[derive(Debug, Fail)]
#[fail(display = "Unknown enum value {}", _0)]
pub struct UnknownEnumValue(u32);

impl UnknownEnumValue {
    pub fn value(&self) -> u32 {
        self.0
    }
}

/// Thin wrapper around the typed enum values that allow us to transport
/// unknown enum values.
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum Enum<E: Copy + PartialEq> {
    Recognized(E),
    Unrecognized(u32),
}

impl<E: Copy + PartialEq> Enum<E> {
    /// Extract the inner enum type as a result.
    ///
    /// Ex:
    ///   let inner = some_argument.as_enum()?;
    pub fn as_enum(&self) -> Result<E, UnknownEnumValue> {
        match *self {
            Enum::Recognized(e) => Ok(e),
            Enum::Unrecognized(i) => Err(UnknownEnumValue(i)),
        }
    }
}

impl<E: Copy + PartialEq + Display> Display for Enum<E> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> Result<(), fmt::Error> {
        match self {
            Enum::Recognized(e) => write!(f, "{}", e),
            Enum::Unrecognized(v) => write!(f, "{}", v),
        }
    }
}

/// A `NewObject` is a type-safe wrapper around a 'new_id' argument that has
/// a static wayland interface. This wrapper will enforce that the object is
/// only implemented by types that can receive wayland messages for the
/// expected interface.
pub struct NewObject<I: Interface + 'static>(PhantomData<I>, ObjectId);
impl<I: Interface + 'static> Display for NewObject<I> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> Result<(), fmt::Error> {
        write!(f, "NewObject<{}>({})", I::NAME, self.1)
    }
}
impl<I: Interface + 'static> Debug for NewObject<I> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> Result<(), fmt::Error> {
        write!(f, "{}", self)
    }
}

/// Support turning raw `ObjectId`s into `NewObject`s.
///
/// Ex:
///   let id: ObjectId = 3;
///   let new_object: NewObject<MyInterface> = id.into();
impl<I: Interface + 'static> From<ObjectId> for NewObject<I> {
    fn from(id: ObjectId) -> Self {
        Self::from_id(id)
    }
}

impl<I: Interface + 'static> NewObject<I> {
    pub fn from_id(id: ObjectId) -> Self {
        NewObject(PhantomData, id)
    }

    pub fn id(&self) -> ObjectId {
        self.1
    }
}
