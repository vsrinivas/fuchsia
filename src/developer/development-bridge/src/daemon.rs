// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::{LOCAL_SOCAT, MAX_RETRY_COUNT, SOCAT, SOCKET, TARGET_SOCAT},
    anyhow::{Context, Error},
    ascendd_lib::run_ascendd,
    fidl::endpoints::{ClientEnd, RequestStream, ServiceMarker},
    fidl_fidl_developer_bridge::{DaemonMarker, DaemonRequest, DaemonRequestStream},
    fidl_fuchsia_developer_remotecontrol::{FdbRemoteControlMarker, FdbRemoteControlProxy},
    fidl_fuchsia_overnet::{
        ServiceConsumerProxyInterface, ServiceProviderRequest, ServiceProviderRequestStream,
    },
    fidl_fuchsia_overnet_protocol::NodeId,
    futures::prelude::*,
    hoist::spawn,
    std::path::Path,
    std::process::Command,
};

mod constants;

async fn start_ascendd() {
    if Path::new(SOCKET).exists() {
        log::info!("Ascendd already started.");
    } else {
        log::info!("Starting ascendd");
        spawn(async move {
            run_ascendd(SOCKET.to_string()).await.unwrap();
        });
        log::info!("Connecting to target");
        Command::new(SOCAT).arg(LOCAL_SOCAT).arg(TARGET_SOCAT).spawn().unwrap();
    }
}

// Daemon
pub struct Daemon {
    remote_control_proxy: FdbRemoteControlProxy,
}

impl Daemon {
    pub async fn new() -> Result<Daemon, Error> {
        let mut peer_id = Daemon::find_remote_control().await?;
        let remote_control_proxy = Daemon::create_remote_control_proxy(&mut peer_id).await?;
        Ok(Daemon { remote_control_proxy })
    }

    pub fn new_with_proxy(remote_control_proxy: FdbRemoteControlProxy) -> Daemon {
        Daemon { remote_control_proxy }
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

    async fn create_remote_control_proxy(id: &mut NodeId) -> Result<FdbRemoteControlProxy, Error> {
        let svc = hoist::connect_as_service_consumer()?;
        let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
        svc.connect_to_service(id, FdbRemoteControlMarker::NAME, s)?;
        let proxy = fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
        Ok(FdbRemoteControlProxy::new(proxy))
    }

    async fn find_remote_control() -> Result<NodeId, Error> {
        let svc = hoist::connect_as_service_consumer()?;
        // Sometimes list_peers doesn't properly report the published services - retry a few times
        // but don't loop indefinitely.
        for _ in 0..MAX_RETRY_COUNT {
            let peers = svc.list_peers().await?;
            log::trace!("Got peers: {:?}", peers);
            for peer in peers {
                if peer.description.services.is_none() {
                    continue;
                }
                if peer
                    .description
                    .services
                    .unwrap()
                    .iter()
                    .find(|name| *name == FdbRemoteControlMarker::NAME)
                    .is_none()
                {
                    continue;
                }
                return Ok(peer.id);
            }
        }
        panic!("No remote control found.  Is a device connected?")
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
            DaemonRequest::RunComponent { component_url, args, responder } => {
                if !quiet {
                    log::info!(
                        "Received run component request for string {:?}:{:?}",
                        component_url,
                        args
                    );
                }
                let response = self
                    .remote_control_proxy
                    .run_component(&component_url, &mut args.iter().map(|s| s.as_str()))
                    .await?;
                responder.send(response).context("error sending response")?;
            }
            _ => {
                log::info!("Unsupported method");
            }
        }
        Ok(())
    }
}

////////////////////////////////////////////////////////////////////////////////
// Overnet Server implementation

fn spawn_daemon_server(stream: DaemonRequestStream, quiet: bool) {
    hoist::spawn(async move {
        Daemon::new()
            .await
            .unwrap()
            .handle_requests_from_stream(stream, quiet)
            .await
            .unwrap_or_else(|err| panic!("Fatal error handling request: {:?}", err));
    });
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
        log::info!("Starting ascendd");
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
    use fidl_fuchsia_developer_remotecontrol::{
        FdbRemoteControlMarker, FdbRemoteControlProxy, FdbRemoteControlRequest,
        RunComponentResponse,
    };

    fn spawn_daemon_server_with_fake_remote_control(stream: DaemonRequestStream) {
        hoist::spawn(async move {
            Daemon::new_with_proxy(setup_fake_remote_control_service())
                .handle_requests_from_stream(stream, false)
                .await
                .unwrap_or_else(|err| panic!("Fatal error handling request: {:?}", err));
        });
    }

    fn setup_fake_remote_control_service() -> FdbRemoteControlProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<FdbRemoteControlMarker>().unwrap();

        hoist::spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(FdbRemoteControlRequest::RunComponent {
                        component_url,
                        args,
                        responder,
                    }) => {
                        let response = RunComponentResponse {
                            component_stdout: Some(component_url),
                            component_stderr: Some(args.join(",")),
                            exit_code: Some(0),
                        };
                        let _ = responder.send(response);
                    }
                    _ => assert!(false),
                }
            }
        });

        proxy
    }

    #[test]
    fn test_echo() {
        let echo = "test-echo";
        let (daemon_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
        hoist::run(async move {
            spawn_daemon_server_with_fake_remote_control(stream);
            let echoed = daemon_proxy.echo_string(echo).await.unwrap();
            assert_eq!(echoed, echo);
        });
    }

    #[test]
    fn test_run_component() {
        let url = "http://test.com";
        let args = vec!["test1".to_string(), "test2".to_string()];
        let (daemon_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
        hoist::run(async move {
            spawn_daemon_server_with_fake_remote_control(stream);
            let response = daemon_proxy
                .run_component(url, &mut args.iter().map(|s| s.as_str()))
                .await
                .unwrap();
            assert_eq!(response.exit_code.unwrap(), 0);
            assert_eq!(response.component_stdout.unwrap(), url.to_string());
            assert_eq!(response.component_stderr.unwrap(), args.join(","));
        });
    }
}
