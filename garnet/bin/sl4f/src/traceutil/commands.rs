// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::traceutil::{facade::TraceutilFacade, types::TraceutilMethod};
use failure::Error;
use serde_json::Value;
use std::sync::Arc;

// Takes SL4F method command and executes corresponding Traceutil methods.
pub async fn traceutil_method_to_fidl(
    method_name: String,
    args: Value,
    facade: Arc<TraceutilFacade>,
) -> Result<Value, Error> {
    match method_name.parse()? {
        TraceutilMethod::GetTraceFile => await!(facade.get_trace_file(args)),
    }
}
