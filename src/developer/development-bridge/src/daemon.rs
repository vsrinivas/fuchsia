// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    ascendd_lib::run_ascendd,
    fidl::endpoints::{ClientEnd, RequestStream, ServiceMarker},
    fidl_fidl_developer_bridge::{DaemonMarker, DaemonRequest, DaemonRequestStream},
    fidl_fuchsia_overnet::{ServiceProviderRequest, ServiceProviderRequestStream},
    futures::prelude::*,
    hoist::spawn,
    std::path::Path,
};
mod constants;
use constants::{CONFIG_JSON_FILE, SOCKET};
mod config;
use config::Config;

async fn start_ascendd() {
    if Path::new(SOCKET).exists() {
        log::info!("Ascendd already started.");
    } else {
        log::info!("Starting ascendd");
        spawn(async move {
            run_ascendd(SOCKET.to_string()).await.unwrap();
        });
    }
}

// Daemon
pub struct Daemon {}

impl Daemon {
    pub fn new() -> Daemon {
        Self {}
    }

    pub async fn handle_requests_from_stream<'a>(
        &'a self,
        mut stream: DaemonRequestStream,
        quiet: bool,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            self.handle_request(req, quiet).await?;
        }
        Ok(())
    }

    pub async fn handle_request<'a>(
        &'a self,
        req: DaemonRequest,
        quiet: bool,
    ) -> Result<(), Error> {
        match req {
            DaemonRequest::EchoString { value, responder } => {
                if !quiet {
                    log::info!("Received echo request for string {:?}", value);
                }
                responder.send(value.as_ref()).context("error sending response")?;
                if !quiet {
                    log::info!("echo response sent successfully");
                }
            }
        }
        Ok(())
    }
}

////////////////////////////////////////////////////////////////////////////////
// Overnet Server implementation

fn spawn_daemon_server(stream: DaemonRequestStream, quiet: bool) {
    hoist::spawn(
        async move {
            Daemon::new()
                .handle_requests_from_stream(stream, quiet)
                .await
                .unwrap_or_else(|err| panic!("Fatal error handling request: {:?}", err));
            Ok(())
        }
        .unwrap_or_else(|e: anyhow::Error| log::trace!("{:?}", e)),
    );
}

async fn next_request(
    stream: &mut ServiceProviderRequestStream,
) -> Result<Option<ServiceProviderRequest>, Error> {
    log::trace!("Awaiting request");
    Ok(stream.try_next().await.context("error running service provider server")?)
}

async fn exec_server(quiet: bool) -> Result<(), Error> {
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    let chan = fidl::AsyncChannel::from_channel(s).context("failed to make async channel")?;
    let mut stream = ServiceProviderRequestStream::from_channel(chan);
    hoist::publish_service(DaemonMarker::NAME, ClientEnd::new(p))?;
    while let Some(ServiceProviderRequest::ConnectToService {
        chan,
        info: _,
        control_handle: _control_handle,
    }) = next_request(&mut stream).await?
    {
        if !quiet {
            log::trace!("Received service request for service");
        }
        let chan =
            fidl::AsyncChannel::from_channel(chan).context("failed to make async channel")?;
        spawn_daemon_server(DaemonRequestStream::from_channel(chan), quiet);
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// main

fn main() {
    hoist::run(async move {
        let mut config: Config = Config::new();
        log::info!("Loading configuration");
        if let Err(err) = config.load_from_config_data(CONFIG_JSON_FILE) {
            log::error!("Failed to load configuration file: {}", err);
        }
        start_ascendd().await;
        log::info!("Starting daemon overnet server");
        exec_server(true)
            .await
            .map_err(|e| log::error!("{}", e))
            .expect("could not start daemon server");
    })
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fidl_developer_bridge::DaemonMarker;

    #[test]
    fn test_echo() {
        let echo = "test-echo";
        let (daemon_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
        hoist::run(async move {
            spawn_daemon_server(stream, false);
            let echoed = daemon_proxy.echo_string(echo).await.unwrap();
            assert_eq!(echoed, echo);
        });
    }
}
