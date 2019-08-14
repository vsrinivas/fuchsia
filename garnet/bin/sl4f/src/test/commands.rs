// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use serde_derive::{Deserialize, Serialize};
use serde_json::Value;
use std::sync::Arc;

use crate::test::facade::TestFacade;
use crate::test::types::TestPlan;

#[derive(Serialize, Deserialize)]
struct RunTestArgs {
    test: String,
}

pub async fn test_method_to_fidl(
    method_name: String,
    args_value: Value,
    facade: Arc<TestFacade>,
) -> Result<Value, Error> {
    match method_name.as_str() {
        "RunTest" => {
            let args: RunTestArgs = serde_json::from_value(args_value)?;

            facade.run_test(args.test).await
        }
        "RunPlan" => {
            let plan: TestPlan = serde_json::from_value(args_value)?;

            facade.run_plan(plan).await
        }
        _ => Err(format_err!("Unsupported method {}", method_name)),
    }
}
