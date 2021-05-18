// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Code to raise and lower Response types.  This wraps the generated code to raise and lower
//! the generated Success types with handling for errors and other types of responses.

use crate::{
    generated::translate::{lower_success, raise_success},
    highlevel, lowlevel,
    serde::DeserializeErrorCause,
};

pub fn raise_response(
    lowlevel: &lowlevel::Response,
) -> Result<highlevel::Response, DeserializeErrorCause> {
    let highlevel = match lowlevel {
        lowlevel::Response::Ok => highlevel::Response::Ok,
        lowlevel::Response::Error => highlevel::Response::Error,
        lowlevel::Response::HardcodedError(err) => {
            highlevel::Response::HardcodedError(raise_hardcoded_error(err))
        }
        lowlevel::Response::CmeError(err_code) => highlevel::Response::CmeError(*err_code),
        lowlevel::Response::Success { .. } => {
            let hl = raise_success(lowlevel)?;
            highlevel::Response::Success(hl)
        }
        lowlevel::Response::RawBytes(bytes) => highlevel::Response::RawBytes(bytes.clone()),
    };

    Ok(highlevel)
}

fn raise_hardcoded_error(lowlevel: &lowlevel::HardcodedError) -> highlevel::HardcodedError {
    match lowlevel {
        lowlevel::HardcodedError::NoCarrier => highlevel::HardcodedError::NoCarrier,
        lowlevel::HardcodedError::Busy => highlevel::HardcodedError::Busy,
        lowlevel::HardcodedError::NoAnswer => highlevel::HardcodedError::NoAnswer,
        lowlevel::HardcodedError::Delayed => highlevel::HardcodedError::Delayed,
        lowlevel::HardcodedError::Blacklist => highlevel::HardcodedError::Blacklist,
    }
}

pub fn lower_response(highlevel: &highlevel::Response) -> lowlevel::Response {
    match highlevel {
        highlevel::Response::Ok => lowlevel::Response::Ok,
        highlevel::Response::Error => lowlevel::Response::Error,
        highlevel::Response::HardcodedError(err) => {
            lowlevel::Response::HardcodedError(lower_hardcoded_error(err))
        }
        highlevel::Response::CmeError(err_code) => lowlevel::Response::CmeError(*err_code),
        highlevel::Response::Success(success) => lower_success(success),
        highlevel::Response::RawBytes(bytes) => lowlevel::Response::RawBytes(bytes.clone()),
    }
}

fn lower_hardcoded_error(highlevel: &highlevel::HardcodedError) -> lowlevel::HardcodedError {
    match highlevel {
        highlevel::HardcodedError::NoCarrier => lowlevel::HardcodedError::NoCarrier,
        highlevel::HardcodedError::Busy => lowlevel::HardcodedError::Busy,
        highlevel::HardcodedError::NoAnswer => lowlevel::HardcodedError::NoAnswer,
        highlevel::HardcodedError::Delayed => lowlevel::HardcodedError::Delayed,
        highlevel::HardcodedError::Blacklist => lowlevel::HardcodedError::Blacklist,
    }
}
