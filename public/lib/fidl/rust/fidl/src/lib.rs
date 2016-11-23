// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library and runtime for fidl bindings.

extern crate magenta;
extern crate byteorder;

mod encoding;
mod threadyfuture;
mod message;
mod error;
mod server;
mod interface;
mod cookiemap;
mod client;

pub use encoding::{Encodable, Decodable, EncodablePtr, DecodablePtr, EncodableType};
pub use encoding::{CodableUnion, EncodableNullable, DecodableNullable};
pub use encoding::{encode_handle, decode_handle};
pub use threadyfuture::{Future, Promise};
pub use message::{EncodeBuf, DecodeBuf, MsgType};
pub use error::{Error, Result};
pub use server::{Stub, Server};
pub use interface::InterfacePtr;
pub use client::Client;
