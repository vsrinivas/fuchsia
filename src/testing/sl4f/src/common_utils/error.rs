// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth as bt;
use thiserror::Error;

#[derive(Debug, Error)]
#[error("{}", message)]
pub struct Sl4fError {
    message: String,
}

impl Sl4fError {
    /// Constructs an Error with a message.
    pub fn new(msg: &str) -> Sl4fError {
        Sl4fError { message: msg.to_string() }
    }
}

impl From<bt::Error> for Sl4fError {
    fn from(err: bt::Error) -> Sl4fError {
        Sl4fError {
            message: match err.description {
                Some(d) => d,
                None => "unknown Bluetooth FIDL error".to_string(),
            },
        }
    }
}

impl From<fidl::Error> for Sl4fError {
    fn from(err: fidl::Error) -> Sl4fError {
        Sl4fError { message: format!("FIDL error: {}", err) }
    }
}
