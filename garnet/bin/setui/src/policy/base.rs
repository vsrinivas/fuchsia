// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::policy as audio;
use thiserror::Error;

/// `Request` defines the request space for all policies handled by
/// the Setting Service. Note that the actions that can be taken upon each
/// policy should be defined within each policy's Request enum.
#[allow(dead_code)]
#[derive(PartialEq, Debug, Clone)]
pub enum Request {
    Audio(audio::Request),
}

pub mod response {
    use super::*;

    pub type Response = Result<Payload, Error>;

    /// `Payload` defines the possible successful responses for a request. There
    /// should be a corresponding policy response payload type for each request type.
    #[allow(dead_code)]
    #[derive(PartialEq, Debug, Clone)]
    pub enum Payload {
        Audio(audio::Response),
    }

    /// The possible errors that can be returned from a request. Note that
    /// unlike the request and response space, errors are not type specific.
    #[derive(Error, Debug, Clone, PartialEq)]
    #[allow(dead_code)]
    pub enum Error {
        #[error("Unexpected error")]
        Unexpected,
    }
}
