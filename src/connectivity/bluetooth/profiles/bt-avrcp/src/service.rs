// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::prelude::*,
    fidl_fuchsia_bluetooth_avrcp::*,
    fidl_fuchsia_bluetooth_avrcp_test::*,
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::PeerId,
    fuchsia_component::server::{ServiceFs, ServiceObj},
    fuchsia_zircon as zx,
    futures::{
        self,
        channel::mpsc,
        future::{FutureExt, TryFutureExt},
        stream::{StreamExt, TryStreamExt},
        Future,
    },
    tracing::{info, warn},
};

use crate::{
    browse_controller_service, controller_service, peer::Controller, peer_manager::ServiceRequest,
};

/// Spawns a future that listens and responds to requests for a controller object over FIDL.
fn spawn_avrcp_clients(
    stream: PeerManagerRequestStream,
    sender: mpsc::Sender<ServiceRequest>,
) -> fasync::Task<()> {
    info!("Spawning avrcp client handler");
    fasync::Task::spawn(
        handle_peer_manager_requests(
            stream,
            sender,
            &controller_service::spawn_service,
            &browse_controller_service::spawn_service,
        )
        .unwrap_or_else(|e: anyhow::Error| warn!("AVRCP client handler finished: {:?}", e)),
    )
}

/// Polls the stream for the PeerManager FIDL interface to set target handlers and respond with
/// new controller clients.
pub async fn handle_peer_manager_requests<F, G>(
    mut stream: PeerManagerRequestStream,
    mut sender: mpsc::Sender<ServiceRequest>,
    mut spawn_controller_fn: F,
    mut spawn_browse_controller_fn: G,
) -> Result<(), anyhow::Error>
where
    F: FnMut(Controller, ControllerRequestStream) -> fasync::Task<()>,
    G: FnMut(Controller, BrowseControllerRequestStream) -> fasync::Task<()>,
{
    while let Some(req) = stream.try_next().await? {
        match req {
            PeerManagerRequest::GetBrowseControllerForTarget { peer_id, client, responder } => {
                let peer_id = peer_id.into();
                info!("Received client request for browse controller for peer {}", peer_id);

                match client.into_stream() {
                    Err(err) => {
                        warn!("Unable to take client stream: {:?}", err);
                        responder.send(&mut Err(zx::Status::UNAVAILABLE.into_raw()))?;
                    }
                    Ok(client_stream) => {
                        let (response, pcr) = ServiceRequest::new_controller_request(peer_id);
                        sender.try_send(pcr)?;
                        let controller = response.into_future().await?;
                        // BrowseController can remain connected after PeerManager disconnects.
                        spawn_browse_controller_fn(controller, client_stream).detach();
                        responder.send(&mut Ok(()))?;
                    }
                }
            }
            PeerManagerRequest::GetControllerForTarget { peer_id, client, responder } => {
                let peer_id = peer_id.into();
                info!("Received client request for controller for peer {}", peer_id);

                match client.into_stream() {
                    Err(err) => {
                        warn!("Unable to take client stream: {:?}", err);
                        responder.send(&mut Err(zx::Status::UNAVAILABLE.into_raw()))?;
                    }
                    Ok(client_stream) => {
                        let (response, pcr) = ServiceRequest::new_controller_request(peer_id);
                        sender.try_send(pcr)?;
                        let controller = response.into_future().await?;
                        // Controller can remain connected after PeerManager disconnects.
                        spawn_controller_fn(controller, client_stream).detach();
                        responder.send(&mut Ok(()))?;
                    }
                }
            }
            PeerManagerRequest::SetAbsoluteVolumeHandler { handler, responder } => {
                info!("Received client request to set absolute volume handler");
                match handler.into_proxy() {
                    Ok(absolute_volume_handler) => {
                        let (response, register_absolute_volume_handler_request) =
                            ServiceRequest::new_register_absolute_volume_handler_request(
                                absolute_volume_handler,
                            );
                        sender.try_send(register_absolute_volume_handler_request)?;
                        match response.into_future().await? {
                            Ok(_) => responder.send(&mut Ok(()))?,
                            Err(_) => {
                                responder.send(&mut Err(zx::Status::ALREADY_BOUND.into_raw()))?
                            }
                        }
                    }
                    Err(_) => responder.send(&mut Err(zx::Status::INVALID_ARGS.into_raw()))?,
                };
            }
            PeerManagerRequest::RegisterTargetHandler { handler, responder } => {
                info!("Received client request for registering Target Handler");
                match handler.into_proxy() {
                    Ok(target_handler) => {
                        let (response, register_target_handler_request) =
                            ServiceRequest::new_register_target_handler_request(target_handler);
                        sender.try_send(register_target_handler_request)?;
                        match response.into_future().await? {
                            Ok(_) => responder.send(&mut Ok(()))?,
                            Err(_) => {
                                responder.send(&mut Err(zx::Status::ALREADY_BOUND.into_raw()))?
                            }
                        }
                    }
                    Err(_) => responder.send(&mut Err(zx::Status::INVALID_ARGS.into_raw()))?,
                };
            }
        }
    }
    Ok(())
}

/// spawns a future that listens and responds to requests for a controller object over FIDL.
fn spawn_test_avrcp_clients(
    stream: PeerManagerExtRequestStream,
    sender: mpsc::Sender<ServiceRequest>,
) -> fasync::Task<()> {
    info!("Spawning test avrcp client handler");
    fasync::Task::spawn(
        handle_peer_manager_ext_requests(
            stream,
            sender,
            &controller_service::spawn_ext_service,
            &browse_controller_service::spawn_ext_service,
        )
        .unwrap_or_else(|e: anyhow::Error| warn!("Test AVRCP client handler finished: {:?}", e)),
    )
}

/// Polls the stream for the PeerManagerExt FIDL interface and responds with new test controller clients.
pub async fn handle_peer_manager_ext_requests<F, G>(
    mut stream: PeerManagerExtRequestStream,
    mut sender: mpsc::Sender<ServiceRequest>,
    mut spawn_controller_fn: F,
    mut spawn_browse_controller_fn: G,
) -> Result<(), anyhow::Error>
where
    F: FnMut(Controller, ControllerExtRequestStream) -> fasync::Task<()>,
    G: FnMut(Controller, BrowseControllerExtRequestStream) -> fasync::Task<()>,
{
    while let Some(req) = stream.try_next().await? {
        match req {
            PeerManagerExtRequest::GetBrowseControllerForTarget { peer_id, client, responder } => {
                let peer_id: PeerId = peer_id.into();
                info!("New test connection request for {}", peer_id);

                match client.into_stream() {
                    Err(err) => {
                        warn!("Unable to take test client stream {:?}", err);
                        responder.send(&mut Err(zx::Status::UNAVAILABLE.into_raw()))?;
                    }
                    Ok(client_stream) => {
                        let (response, pcr) = ServiceRequest::new_controller_request(peer_id);
                        sender.try_send(pcr)?;
                        let controller = response.into_future().await?;
                        // BrowseControllerExt can remain connected after PeerManager disconnects.
                        spawn_browse_controller_fn(controller, client_stream).detach();
                        responder.send(&mut Ok(()))?;
                    }
                }
            }
            PeerManagerExtRequest::GetControllerForTarget { peer_id, client, responder } => {
                let peer_id: PeerId = peer_id.into();
                info!("New test connection request for {}", peer_id);

                match client.into_stream() {
                    Err(err) => {
                        warn!("Unable to take test client stream {:?}", err);
                        responder.send(&mut Err(zx::Status::UNAVAILABLE.into_raw()))?;
                    }
                    Ok(client_stream) => {
                        let (response, pcr) = ServiceRequest::new_controller_request(peer_id);
                        sender.try_send(pcr)?;
                        let controller = response.into_future().await?;
                        // ControllerExt can remain connected after PeerManager disconnects.
                        spawn_controller_fn(controller, client_stream).detach();
                        responder.send(&mut Ok(()))?;
                    }
                }
            }
        }
    }
    Ok(())
}

/// Sets up public FIDL services and client handlers.
pub fn run_services(
    mut fs: ServiceFs<ServiceObj<'_, ()>>,
    sender: mpsc::Sender<ServiceRequest>,
) -> Result<impl Future<Output = Result<(), Error>> + '_, Error> {
    let sender_avrcp = sender.clone();
    let sender_test = sender.clone();
    let _ = fs
        .dir("svc")
        .add_fidl_service_at(PeerManagerExtMarker::PROTOCOL_NAME, move |stream| {
            spawn_test_avrcp_clients(stream, sender_test.clone()).detach();
        })
        .add_fidl_service_at(PeerManagerMarker::PROTOCOL_NAME, move |stream| {
            spawn_avrcp_clients(stream, sender_avrcp.clone()).detach();
        });
    let _ = fs.take_and_serve_directory_handle()?;
    info!("Running fidl service");
    Ok(fs.collect::<()>().map(|_| Err(format_err!("FIDL service listener returned"))))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::peer::RemotePeerHandle;
    use crate::peer_manager::TargetDelegate;
    use assert_matches::assert_matches;
    use fidl::endpoints::{create_endpoints, create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_bluetooth_bredr::{ProfileMarker, ProfileProxy};
    use futures::task::Poll;
    use pin_utils::pin_mut;
    use std::sync::Arc;

    fn handle_get_controller(profile_proxy: ProfileProxy, request: ServiceRequest) {
        match request {
            ServiceRequest::GetController { peer_id, reply } => {
                // TODO(jamuraa): Make Controller a trait so we can mock it here.
                let peer = RemotePeerHandle::spawn_peer(
                    peer_id,
                    Arc::new(TargetDelegate::new()),
                    profile_proxy,
                );
                reply.send(Controller::new(peer)).expect("reply should succeed");
            }
            x => panic!("Unexpected request from client stream: {:?}", x),
        }
    }

    fn make_service_spawn_fn<T>(
    ) -> (impl FnMut(Controller, T) -> fasync::Task<()>, mpsc::Receiver<()>) {
        let (mut spawned_sender, spawned_receiver) = mpsc::channel::<()>(1);
        let spawn_fn = move |_controller: Controller, _service_fidl_stream: T| {
            // Signal that the client has been created.
            spawned_sender.try_send(()).expect("couldn't send spawn signal");
            fasync::Task::spawn(async {})
        };
        (spawn_fn, spawned_receiver)
    }

    #[fuchsia::test]
    /// Tests that a request to register a target handler responds correctly.
    fn spawn_avrcp_target() -> Result<(), Error> {
        let mut exec = fasync::TestExecutor::new().expect("TestExecutor should be created");
        let (peer_manager_proxy, peer_manager_requests) =
            create_proxy_and_stream::<PeerManagerMarker>()?;

        let (client_sender, mut service_request_receiver) = mpsc::channel(512);

        let fail_fn = |_controller: Controller, _fidl_stream: ControllerRequestStream| {
            panic!("Shouldn't have spawned a controller!");
        };

        let noop_browse_fn =
            |_controller: Controller, _fidl_stream: BrowseControllerRequestStream| {
                panic!("Shouldn't have spawned a controller!");
            };

        let (target_client, _target_server) =
            create_endpoints::<TargetHandlerMarker>().expect("Target proxy creation");

        let request_fut = peer_manager_proxy.register_target_handler(target_client);
        pin_mut!(request_fut);

        let handler_fut = handle_peer_manager_requests(
            peer_manager_requests,
            client_sender,
            fail_fn,
            noop_browse_fn,
        );
        pin_mut!(handler_fut);

        // Make the request.
        assert!(exec.run_until_stalled(&mut request_fut).is_pending());

        // Running the stream handler should produce a request to register a target
        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());

        let request = service_request_receiver.try_next()?.expect("a request should be made");

        match request {
            ServiceRequest::RegisterTargetHandler { target_handler: _, reply } => {
                reply.send(Ok(())).expect("Reply should succeed");
            }
            x => panic!("Unexpected request from client stream: {:?}", x),
        };

        // The request should be answered.
        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());
        assert_matches!(exec.run_until_stalled(&mut request_fut), Poll::Ready(Ok(Ok(()))));

        drop(peer_manager_proxy);

        // The handler should end when the client is closed.
        assert_matches!(exec.run_until_stalled(&mut handler_fut), Poll::Ready(Ok(())));
        Ok(())
    }

    #[test]
    /// Tests that the client stream handler will spawn a controller when a controller request
    /// successfully sets up a controller.
    fn spawn_avrcp_controllers() -> Result<(), Error> {
        let mut exec = fasync::TestExecutor::new().expect("TestExecutor should be created");
        let (peer_manager_proxy, peer_manager_requests) =
            create_proxy_and_stream::<PeerManagerMarker>()?;

        let (client_sender, mut service_request_receiver) = mpsc::channel(512);

        let (spawn_controller_fn, mut spawned_controller_receiver) =
            make_service_spawn_fn::<ControllerRequestStream>();
        let (spawn_browse_controller_fn, mut spawned_browse_controller_receiver) =
            make_service_spawn_fn::<BrowseControllerRequestStream>();

        let (profile_proxy, _profile_requests) = create_proxy_and_stream::<ProfileMarker>()?;

        let (_c_proxy, controller_server) = create_proxy().expect("Controller proxy creation");
        let (_bc_proxy, bcontroller_server) = create_proxy().expect("Controller proxy creation");

        let handler_fut = handle_peer_manager_requests(
            peer_manager_requests,
            client_sender,
            spawn_controller_fn,
            spawn_browse_controller_fn,
        );
        pin_mut!(handler_fut);

        let request_fut = peer_manager_proxy
            .get_controller_for_target(&mut PeerId(123).into(), controller_server);
        pin_mut!(request_fut);

        // Make the request.
        assert!(exec.run_until_stalled(&mut request_fut).is_pending());

        // Running the stream handler should produce a request for a controller.
        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());

        let request = service_request_receiver.try_next()?.expect("a request should be made");
        handle_get_controller(profile_proxy.clone(), request);

        // The handler should spawn the request after the reply.
        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());

        spawned_controller_receiver.try_next()?.expect("a client should have been spawned");

        assert_matches!(exec.run_until_stalled(&mut request_fut), Poll::Ready(Ok(Ok(()))));

        let request_fut = peer_manager_proxy
            .get_browse_controller_for_target(&mut PeerId(123).into(), bcontroller_server);
        pin_mut!(request_fut);

        // Make the request.
        assert!(exec.run_until_stalled(&mut request_fut).is_pending());

        // Running the stream handler should produce a request for a controller.
        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());

        let request = service_request_receiver.try_next()?.expect("a request should be made");
        handle_get_controller(profile_proxy.clone(), request);

        // The handler should spawn the request after the reply.
        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());

        spawned_browse_controller_receiver.try_next()?.expect("a client should have been spawned");

        assert_matches!(exec.run_until_stalled(&mut request_fut), Poll::Ready(Ok(Ok(()))));

        drop(peer_manager_proxy);

        // The handler should end when the client is closed.
        assert_matches!(exec.run_until_stalled(&mut handler_fut), Poll::Ready(Ok(())));
        Ok(())
    }

    #[test]
    /// Test that getting a controller from the test server (PeerManagerExt) works.
    fn spawn_avrcp_extension_controllers() -> Result<(), Error> {
        let mut exec = fasync::TestExecutor::new().expect("TestExecutor should be created");
        let (peer_manager_ext_proxy, peer_manager_ext_requests) =
            create_proxy_and_stream::<PeerManagerExtMarker>()?;

        let (client_sender, mut service_request_receiver) = mpsc::channel(512);

        let (spawn_controller_fn, mut spawned_controller_receiver) =
            make_service_spawn_fn::<ControllerExtRequestStream>();
        let (spawn_browse_controller_fn, mut spawned_browse_controller_receiver) =
            make_service_spawn_fn::<BrowseControllerExtRequestStream>();

        let (profile_proxy, _profile_requests) = create_proxy_and_stream::<ProfileMarker>()?;

        let (_c_proxy, controller_server) = create_proxy().expect("Controller proxy creation");
        let (_bc_proxy, bcontroller_server) = create_proxy().expect("Controller proxy creation");

        let handler_fut = handle_peer_manager_ext_requests(
            peer_manager_ext_requests,
            client_sender,
            spawn_controller_fn,
            spawn_browse_controller_fn,
        );
        pin_mut!(handler_fut);

        let request_fut = peer_manager_ext_proxy
            .get_controller_for_target(&mut PeerId(123).into(), controller_server);
        pin_mut!(request_fut);

        // Make the request.
        assert!(exec.run_until_stalled(&mut request_fut).is_pending());

        // Running the stream handler should produce a request for a controller.
        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());

        let request = service_request_receiver.try_next()?.expect("a request should be made");
        handle_get_controller(profile_proxy.clone(), request);

        // The handler should spawn the request after the reply.
        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());

        spawned_controller_receiver.try_next()?.expect("a client should have been spawned");

        assert_matches!(exec.run_until_stalled(&mut request_fut), Poll::Ready(Ok(Ok(()))));

        let request_fut = peer_manager_ext_proxy
            .get_browse_controller_for_target(&mut PeerId(123).into(), bcontroller_server);
        pin_mut!(request_fut);

        // Make the request.
        assert!(exec.run_until_stalled(&mut request_fut).is_pending());

        // Running the stream handler should produce a request for a controller.
        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());

        let request = service_request_receiver.try_next()?.expect("a request should be made");
        handle_get_controller(profile_proxy.clone(), request);

        // The handler should spawn the request after the reply.
        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());

        spawned_browse_controller_receiver.try_next()?.expect("a client should have been spawned");

        assert_matches!(exec.run_until_stalled(&mut request_fut), Poll::Ready(Ok(Ok(()))));

        drop(peer_manager_ext_proxy);

        // The handler should end when the client is closed.
        assert_matches!(exec.run_until_stalled(&mut handler_fut), Poll::Ready(Ok(())));
        Ok(())
    }
}
