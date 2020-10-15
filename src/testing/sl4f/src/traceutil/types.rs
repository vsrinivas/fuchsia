// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Enum for supported Traceutil commands.
pub enum TraceutilMethod {
    GetTraceFile,
}

impl std::str::FromStr for TraceutilMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "GetTraceFile" => Ok(TraceutilMethod::GetTraceFile),
            _ => return Err(format_err!("invalid Traceutil Facade method: {}", method)),
        }
    }
}
