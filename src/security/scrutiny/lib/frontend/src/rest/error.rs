// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

#[derive(Error, Debug)]
pub enum RestError {
    #[error("Scrutiny service adress is already in use: {}", addr)]
    PortInUse { addr: String },
}

impl RestError {
    pub fn port_in_use(addr: impl Into<String>) -> RestError {
        RestError::PortInUse { addr: addr.into() }
    }
}
