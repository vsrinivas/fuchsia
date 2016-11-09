// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Error (common to all fidl operations)

pub type Result<T> = ::std::result::Result<T, Error>;

#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    Invalid,
    OutOfRange,
    NotNullable,
    Utf8Error,
    InvalidHandle,
    UnknownOrdinal,
    UnknownUnionTag,
    RemoteClosed,
}
