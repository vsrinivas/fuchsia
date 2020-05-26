// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles diagnostics requests

use {
    crate::{future_help::log_errors, router::Router, runtime::spawn},
    anyhow::{Context as _, Error},
    fidl::{
        endpoints::{ClientEnd, RequestStream, ServiceMarker},
        AsyncChannel, Channel,
    },
    fidl_fuchsia_overnet::{ServiceProviderRequest, ServiceProviderRequestStream},
    fidl_fuchsia_overnet_protocol::{
        DiagnosticMarker, DiagnosticRequest, DiagnosticRequestStream, ProbeResult, ProbeSelector,
    },
    futures::prelude::*,
    std::rc::Rc,
};

// Helper: if a bit is set in a probe selector, return Some(make()), else return None
async fn if_probe_has_bit<R>(
    probe: ProbeSelector,
    bit: ProbeSelector,
    make: impl Future<Output = R>,
) -> Option<R> {
    if probe & bit == bit {
        Some(make.await)
    } else {
        None
    }
}

pub fn spawn_diagostic_service_request_handler(
    router: Rc<Router>,
    implementation: fidl_fuchsia_overnet_protocol::Implementation,
) {
    spawn(log_errors(
        async move {
            let (s, p) = Channel::create().context("failed to create zx channel")?;
            let chan = AsyncChannel::from_channel(s).context("failed to make async channel")?;
            router.register_service(DiagnosticMarker::NAME.to_string(), ClientEnd::new(p)).await?;
            handle_diagnostic_service_requests(
                router,
                implementation,
                ServiceProviderRequestStream::from_channel(chan),
            )
            .await
        },
        "Failed handling diagnostic requests",
    ));
}

async fn handle_diagnostic_service_requests(
    router: Rc<Router>,
    implementation: fidl_fuchsia_overnet_protocol::Implementation,
    mut stream: ServiceProviderRequestStream,
) -> Result<(), Error> {
    while let Some(ServiceProviderRequest::ConnectToService { chan, info: _, control_handle: _ }) =
        stream.try_next().await.context("awaiting next diagnostic stream request")?
    {
        let stream = DiagnosticRequestStream::from_channel(fidl::AsyncChannel::from_channel(chan)?);
        spawn(log_errors(
            handle_diagnostic_requests(router.clone(), implementation, stream),
            "Failed handling diagnostics requests",
        ));
    }
    Ok(())
}

async fn handle_diagnostic_requests(
    router: Rc<Router>,
    implementation: fidl_fuchsia_overnet_protocol::Implementation,
    mut stream: DiagnosticRequestStream,
) -> Result<(), Error> {
    while let Some(req) = stream.try_next().await.context("awaiting next diagnostic request")? {
        match req {
            DiagnosticRequest::Probe { selector, responder } => {
                let res = responder.send(ProbeResult {
                    node_description: if_probe_has_bit(
                        selector,
                        ProbeSelector::NodeDescription,
                        futures::future::ready(fidl_fuchsia_overnet_protocol::NodeDescription {
                            #[cfg(target_os = "fuchsia")]
                            operating_system: Some(
                                fidl_fuchsia_overnet_protocol::OperatingSystem::Fuchsia,
                            ),
                            #[cfg(target_os = "linux")]
                            operating_system: Some(
                                fidl_fuchsia_overnet_protocol::OperatingSystem::Linux,
                            ),
                            #[cfg(target_os = "macos")]
                            operating_system: Some(
                                fidl_fuchsia_overnet_protocol::OperatingSystem::Mac,
                            ),
                            implementation: Some(implementation),
                        }),
                    )
                    .await,
                    links: if_probe_has_bit(
                        selector,
                        ProbeSelector::Links,
                        router.link_diagnostics(),
                    )
                    .await,
                    peer_connections: if_probe_has_bit(
                        selector,
                        ProbeSelector::PeerConnections,
                        router.peer_diagnostics(),
                    )
                    .await,
                });
                if let Err(e) = res {
                    log::warn!("Failed handling probe: {:?}", e);
                }
            }
        }
    }
    Ok(())
}
