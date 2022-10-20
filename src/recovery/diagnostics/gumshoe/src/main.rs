// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod device_info;
mod handlebars_utils;
mod responder;
mod webserver;

use crate::device_info::DeviceInfoImpl;
use crate::handlebars_utils::TemplateEngine;
use crate::responder::ResponderImpl;
use crate::webserver::{WebServer, WebServerImpl};
use anyhow::Error;
use futures::lock::Mutex;
use handlebars::Handlebars;
use std::sync::Arc;

const LISTENING_PORT: u16 = 8080;

/// Send gumshoe on a stakeout. While on a stakeout, gumshoe responds
/// to HTTP requests on their webserver's listening port. Returns the
/// WebServer's run() result: a setup error (eg, if bind() fails), or
/// (more likely) run() never returns because waits indefinitely on
/// new connections.
async fn stakeout(
    web_server: &dyn WebServer,
    mut template_engine: Box<dyn TemplateEngine>,
) -> Result<(), Error> {
    // Populate the template engine with templates.
    handlebars_utils::register_template_resources(
        &mut template_engine,
        // TODO(b/253514278): Automatically register all files under component's template/ directory.
        vec!["404", "chrome", "index", "info"],
    )?;

    // Gather stable device data (SerialNumber, Factory Info, etc) used when rendering templates.
    let boxed_device_info = Box::new(DeviceInfoImpl::new());

    // Construct a responder for generating HTTP responses from HTTP requests.
    let responder_impl = ResponderImpl::new(template_engine, boxed_device_info);
    let responder = Arc::new(Mutex::new(responder_impl));

    // Start handling incoming web requests using the responder.
    web_server.run(LISTENING_PORT, responder).await
}

#[fuchsia::main]
async fn main() {
    let web_server_impl = WebServerImpl {};
    let template_engine = Box::new(Handlebars::new());

    match stakeout(&web_server_impl, template_engine).await {
        Ok(_) => {
            eprintln!("Stakeout completed");
        }
        Err(e) => {
            eprintln!("Stakeout terminated with error: {:?}", e);
        }
    };
}

#[cfg(test)]
mod tests {
    use crate::handlebars_utils::MockTemplateEngine;
    use crate::stakeout;
    use crate::webserver::MockWebServer;
    use anyhow::anyhow;
    use fuchsia_async as fasync;

    /// Verifies we run webserver after initializing templates.
    #[fasync::run_singlethreaded(test)]
    async fn gumshoe_starts_webserver_after_registering_templates() {
        let mut web_server = MockWebServer::new();
        let mut template_engine = MockTemplateEngine::new();

        // Expect 4 templates to be registered during the stakeout.
        template_engine.expect_register_resource().times(4).returning(|_, _| Ok(()));

        // Expect the webserver to be started (and simulate exiting immediately).
        web_server.expect_run().times(1).returning(|_, _| Ok(()));

        assert!(!stakeout(&web_server, Box::new(template_engine)).await.is_err());
    }

    /// Verifies a failure to register a template terminates startup (and webserver doesn't start).
    #[fasync::run_singlethreaded(test)]
    async fn gumshoe_quits_when_resource_registration_fails() {
        let web_server = MockWebServer::new();
        let mut template_engine = MockTemplateEngine::new();

        // Simulate failing to register the first template.
        template_engine
            .expect_register_resource()
            .times(1)
            .returning(|_, _| Err(anyhow!("Registration Failed!")));

        assert!(stakeout(&web_server, Box::new(template_engine)).await.is_err());
    }

    /// Verifies WebServer.run() errors are percolated up from stakeout().
    #[fasync::run_singlethreaded(test)]
    async fn stakeout_percolates_webserver_error() {
        let mut web_server = MockWebServer::new();
        let mut template_engine = MockTemplateEngine::new();

        template_engine.expect_register_resource().times(4).returning(|_, _| Ok(()));

        // Expect the webserver to be started (and simulate exiting immediately with error).
        web_server.expect_run().times(1).returning(|_, _| Err(anyhow!("WebServer Error!")));

        assert!(stakeout(&web_server, Box::new(template_engine)).await.is_err());
    }
}
