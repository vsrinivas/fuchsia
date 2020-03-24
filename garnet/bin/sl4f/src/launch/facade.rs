// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use crate::launch::types::{LaunchRequest, LaunchResult};
use anyhow::Error;
use fidl_fuchsia_sys::ComponentControllerEvent;
use fuchsia_component::client;
use fuchsia_syslog::macros::fx_log_info;
use fuchsia_syslog::macros::*;
use futures::StreamExt;
use serde_json::{from_value, Value};

/// Perform Launch fidl operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct LaunchFacade {}

impl LaunchFacade {
    pub fn new() -> LaunchFacade {
        LaunchFacade {}
    }

    /// Parse component url and return app created by launch function
    /// # Arguments
    /// * `args`: will be parsed to LaunchRequest
    /// * `url`: url of the component
    /// * `arguments`: optional arguments for the component
    async fn create_launch_app(&self, args: Value) -> Result<client::App, Error> {
        let tag = "LaunchFacade::create_launch_app";

        let req: LaunchRequest = from_value(args)?;
        // Building the component url from the name of component.
        let component_url = match req.url {
            Some(x) => {
                fx_log_info!(
                    "Executing Launch {} in Launch Facade with arguments {:?}.",
                    x,
                    req.arguments
                );
                let url = format!("fuchsia-pkg://fuchsia.com/{}#meta/{}.cmx", x, x).to_string();
                url
            }
            None => return Err(format_err!("Need component url to launch")),
        };

        let launcher = match client::launcher() {
            Ok(r) => r,
            Err(err) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to get launcher service: {}", err)
            ),
        };
        let app = client::launch(&launcher, component_url.to_string(), req.arguments)?;
        Ok(app)
    }

    /// Launch component with url and optional arguments and detach directly
    /// # Arguments
    /// * `args`: will be parsed to LaunchRequest in create_launch_app
    /// * `url`: url of the component
    /// * `arguments`: optional arguments for the component
    pub async fn launch(&self, args: Value) -> Result<LaunchResult, Error> {
        let tag = "LaunchFacade::launch";
        let launch_app = Some(self.create_launch_app(args).await?);
        let app = match launch_app {
            Some(p) => p,
            None => fx_err_and_bail!(&with_line!(tag), "Failed to launch component."),
        };

        let mut component_stream = app.controller().take_event_stream();
        match component_stream
            .next()
            .await
            .expect("component event stream ended before termination event")?
        {
            // detach if succeeds
            ComponentControllerEvent::OnDirectoryReady {} => {
                app.controller().detach()?;
            }
            // if there's exception (like url package not found, return fail)
            ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
                bail!(
                    "Component terminated unexpectedly. Code: {}. Reason: {:?}",
                    return_code,
                    termination_reason
                );
            }
        }
        Ok(LaunchResult::Success)
    }
}
