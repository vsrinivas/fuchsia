// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use base64;
use serde_json::{to_value, Value};

use std::collections::HashMap;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};

/// Perform Traceutil operations.
///
/// Note this object is shared among all threads created by server.
///
/// This facade does not hold onto a Traceutil proxy as the server may be
/// long-running while individual tests set up and tear down Traceutil.
///
/// WARNING: Use of this facade is discouraged as its functionality is only to download traces that
/// were collected through other means (such as running the trace binary over ssh). Instead, see
/// TracingFacade, which allows for control of the tracing system as well.
#[derive(Debug)]
pub struct TraceutilFacade {}

// Chunk size of 8 MiB. Because we read a full chunk into memory and convert it
// to base64, in cases where the system is close to OOM, a small chunk size
// makes it more likely that we can successfully download the trace. On the other
// hand, a very small chunk size slows down the trace download. Empirically, 8 MiB
// seems to be a reasonable compromise.
const MAX_CHUNK_SIZE: usize = 8 * 1024 * 1024;

impl TraceutilFacade {
    pub fn new() -> TraceutilFacade {
        TraceutilFacade {}
    }

    /// Gets data from the specified path starting from an optional offset.
    ///
    /// Loading and returning the entire file is problematic for large trace files,
    /// so will return up to |MAX_CHUNK_SIZE| bytes at once. The bytes of the file are
    /// returned in a field called |data|. If there is more data to read, then a field
    /// |next_offset| will be returned that indicates where it left off.
    pub async fn get_trace_file(&self, args: Value) -> Result<Value, Error> {
        let path = args.get("path").ok_or(format_err!("GetTraceFile failed, no path"))?;
        let path = path.as_str().ok_or(format_err!("GetTraceFile failed, path not string"))?;
        let offset = args.get("offset").and_then(Value::as_u64).unwrap_or(0);

        let mut file = File::open(path)?;
        file.seek(SeekFrom::Start(offset))?;

        let mut contents = Vec::new();
        file.by_ref().take(MAX_CHUNK_SIZE as u64).read_to_end(&mut contents)?;

        let encoded_contents = base64::encode(&contents);

        let mut result: HashMap<String, Value> = HashMap::new();
        result.insert("data".to_owned(), to_value(encoded_contents)?);
        if contents.len() == MAX_CHUNK_SIZE {
            let new_offset = file.seek(SeekFrom::Current(0))?;
            result.insert("next_offset".to_owned(), to_value(new_offset)?);
        }

        Ok(to_value(result)?)
    }
}
