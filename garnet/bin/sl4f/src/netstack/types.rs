// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Enum for supported FIDL commands.
pub enum NetstackMethod {
    ListInterfaces,
}

impl std::str::FromStr for NetstackMethod {
    type Err = failure::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "ListInterfaces" => Ok(NetstackMethod::ListInterfaces),
            _ => bail!("invalid Netstack Client FIDL method: {}", method),
        }
    }
}
