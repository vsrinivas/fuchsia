// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::input_report::facade::InputReportFacade;
use crate::input_report::types::{InputDeviceMatchArgs, InputReportMethod};
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};
use std::str::FromStr;

#[async_trait(?Send)]
impl Facade for InputReportFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        let match_args = InputDeviceMatchArgs::from(args);
        match InputReportMethod::from_str(&method).unwrap() {
            InputReportMethod::GetReports => Ok(to_value(self.get_reports(match_args).await?)?),
            InputReportMethod::GetDescriptor => {
                Ok(to_value(self.get_descriptor(match_args).await?)?)
            }
            // TODO(bradenkell): Add SendOutputReport if it ends up being needed.
            _ => bail!("Invalid InputReport FIDL method: {:?}", method),
        }
    }
}
