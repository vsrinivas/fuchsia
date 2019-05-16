// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use base64;
use failure::Error;
use serde_json::{to_value, Value};

use std::fs::File;
use std::io::Read;

/// Perform Traceutil operations.
///
/// Note this object is shared among all threads created by server.
///
/// This facade does not hold onto a Traceutil proxy as the server may be
/// long-running while individual tests set up and tear down Traceutil.
#[derive(Debug)]
pub struct TraceutilFacade {}

impl TraceutilFacade {
    pub fn new() -> TraceutilFacade {
        TraceutilFacade {}
    }

    pub async fn get_trace_file(&self, args: Value) -> Result<Value, Error> {
        let path = args.get("path").ok_or(format_err!("GetTraceFile failed, no path"))?;
        let path = path.as_str().ok_or(format_err!("GetTraceFile failed, path not string"))?;

        let mut file = File::open(path)?;
        let mut contents = Vec::new();
        file.read_to_end(&mut contents)?;

        let encoded_contents = base64::encode(&contents);

        Ok(to_value(encoded_contents)?)
    }
}
