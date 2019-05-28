// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

#[macro_use]
mod log_macros;
mod buffer_collection_constraints;
mod buffer_set;
mod elementary_stream;
mod input_packet_stream;
mod output_validator;
mod stream;
mod stream_runner;
mod test_spec;

pub use crate::elementary_stream::*;
pub use crate::output_validator::*;
pub use crate::stream::*;
pub use crate::test_spec::*;
use failure::{Error, Fail};

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Fail, Debug)]
#[fail(display = "FatalError: {}", _0)]
pub struct FatalError(pub String);
