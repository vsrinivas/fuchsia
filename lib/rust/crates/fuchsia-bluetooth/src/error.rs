// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth as bt;

/// Error type that can be constructed from a Bluetooth FIDL Error or from on its own.
#[derive(Debug, Fail)]
#[fail(display = "{}", message)]
pub struct Error {
    message: String,
}

impl Error {
    /// Constructs an Error with a message.
    pub fn new(msg: &str) -> Error {
        Error {
            message: msg.to_string(),
        }
    }
}

impl From<bt::Error> for Error {
    fn from(err: bt::Error) -> Error {
        Error {
            message: match err.description {
                Some(d) => d,
                None => "unknown Bluetooth FIDL error".to_string(),
            },
        }
    }
}
