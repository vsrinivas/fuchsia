// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains types used for sending and receiving messages to and from the storage
//! agent.

use crate::base::{SettingInfo, SettingType};
use crate::handler::setting_handler::persist::UpdateState;

/// `Payload` wraps the request and response payloads.
#[derive(Clone, PartialEq, Debug)]
pub enum Payload {
    Request(StorageRequest),
    Response(StorageResponse),
}

/// `StorageRequest` contains all of the requests that can be made to the storage agent.
#[derive(Clone, PartialEq, Debug)]
pub enum StorageRequest {
    /// A read requests for the corresponding [`SettingInfo`] of this `SettingType`.
    Read(SettingType),
    /// A write requests for this [`SettingInfo`]. The `bool` is for specifying whether
    /// the data needs to be immediately flushed to disk or not.
    Write(SettingInfo, bool),
}

/// `StorageResponse` contains the corresponding result types to the matching [`StorageRequest`]
/// variants of the same name.
#[derive(Clone, PartialEq, Debug)]
pub enum StorageResponse {
    /// The setting info read from storage in response to a [`StorageRequest::Read`]
    Read(SettingInfo),
    /// The result of a write request with either the [`UpdateState`] after a succesful write
    /// or a formatted error describing why the write could not occur.
    Write(Result<UpdateState, Error>),
}

/// `Error` encapsulates a formatted error the occurs due to write failures.
#[derive(Clone, PartialEq, Debug)]
pub struct Error {
    /// The error message.
    pub message: String,
}
