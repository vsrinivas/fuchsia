// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde::{Deserialize, Serialize};
use serde_json::Value;

use crate::test::facade::TestFacade;
use crate::test::types::TestPlan;

#[derive(Serialize, Deserialize)]
struct RunTestArgs {
    test: String,
}

#[async_trait(?Send)]
impl Facade for TestFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_str() {
            "RunTest" => {
                let args: RunTestArgs = serde_json::from_value(args)?;

                self.run_test(args.test).await
            }
            "RunPlan" => {
                let plan: TestPlan = serde_json::from_value(args)?;

                self.run_plan(plan).await
            }
            _ => Err(format_err!("Unsupported method {}", method)),
        }
    }
}
