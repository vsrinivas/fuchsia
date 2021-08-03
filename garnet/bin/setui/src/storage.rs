// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains types used for sending and receiving messages to and from the storage
//! agent.

use crate::base::{SettingInfo, SettingType};
use crate::handler::setting_handler::persist::UpdateState;
use crate::policy::{PolicyInfo, PolicyType};
use crate::trace::TracingNonce;

/// `Payload` wraps the request and response payloads.
#[derive(Clone, PartialEq, Debug)]
pub enum Payload {
    Request(StorageRequest),
    Response(StorageResponse),
}

/// `StorageRequest` contains all of the requests that can be made to the storage agent.
#[derive(Clone, PartialEq, Debug)]
pub enum StorageRequest {
    /// A read requests for the corresponding [`StorageInfo`] of this `StorageType`.
    Read(StorageType, TracingNonce),
    /// A write requests for this [`StorageInfo`]. The `bool` is for specifying whether
    /// the data needs to be immediately flushed to disk or not.
    Write(StorageInfo, bool, TracingNonce),
}

#[derive(Clone, PartialEq, Debug)]
pub enum StorageType {
    SettingType(SettingType),
    PolicyType(PolicyType),
}

impl From<SettingType> for StorageType {
    fn from(setting_type: SettingType) -> Self {
        StorageType::SettingType(setting_type)
    }
}

impl From<PolicyType> for StorageType {
    fn from(policy_data_type: PolicyType) -> Self {
        StorageType::PolicyType(policy_data_type)
    }
}

#[derive(Clone, PartialEq, Debug)]
pub enum StorageInfo {
    SettingInfo(SettingInfo),
    PolicyInfo(PolicyInfo),
}

impl From<SettingInfo> for StorageInfo {
    fn from(setting_info: SettingInfo) -> Self {
        StorageInfo::SettingInfo(setting_info)
    }
}

impl From<PolicyInfo> for StorageInfo {
    fn from(policy_info: PolicyInfo) -> Self {
        StorageInfo::PolicyInfo(policy_info)
    }
}

/// `StorageResponse` contains the corresponding result types to the matching [`StorageRequest`]
/// variants of the same name.
#[derive(Clone, PartialEq, Debug)]
pub enum StorageResponse {
    /// The storage info read from storage in response to a [`StorageRequest::Read`]
    Read(StorageInfo),
    /// The result of a write request with either the [`UpdateState`] after a successful write
    /// or a formatted error describing why the write could not occur.
    Write(Result<UpdateState, Error>),
}

/// `Error` encapsulates a formatted error the occurs due to write failures.
#[derive(Clone, PartialEq, Debug)]
pub struct Error {
    /// The error message.
    pub message: String,
}
