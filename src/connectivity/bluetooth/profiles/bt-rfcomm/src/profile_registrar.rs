// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    async_utils::stream_epitaph::{StreamItem, StreamWithEpitaph, WithEpitaph},
    fidl::endpoints::{create_request_stream, ClientEnd},
    fidl_fuchsia_bluetooth::ErrorCode,
    fidl_fuchsia_bluetooth_bredr as bredr, fuchsia_async as fasync,
    fuchsia_bluetooth::{
        profile::{psm_from_protocol, ChannelParameters, ServiceDefinition},
        types::PeerId,
        util::CollectExt,
    },
    futures::{
        self,
        channel::mpsc,
        future::BoxFuture,
        select,
        sink::SinkExt,
        stream::{FuturesUnordered, StreamExt},
        Future, FutureExt,
    },
    log::{error, info, trace},
    std::{
        collections::HashSet,
        convert::{TryFrom, TryInto},
    },
};

use crate::profile::*;
use crate::rfcomm::RfcommServer;
use crate::types::{AdvertiseParams, ServiceGroup, ServiceGroupHandle, Services};

/// The returned result of a Profile.Advertise request.
enum AdvertiseResult {
    /// The Advertise request needed RFCOMM - the client's event stream is returned.
    EventStream(StreamWithEpitaph<bredr::ConnectionReceiverEventStream, ServiceGroupHandle>),

    /// The Advertise request did not need RFCOMM - the request is relayed directly
    /// to the upstream server. Because Profile.Advertise returns when the advertisement finishes,
    /// we return a Future that relays the result to the client.
    AdvertiseRelay(BoxFuture<'static, ()>),
}

/// A connection request from the upstream server.
#[derive(Debug)]
enum ConnectionEvent {
    /// A normal connection request.
    Request(bredr::ConnectionReceiverRequest),
    /// The upstream server canceled the advertisement, for any reason.
    AdvertisementCanceled,
}

/// The tasks associated with a service advertisement.
struct AdvertiseTasks {
    /// The Profile.Advertise() Future that resolves when advertisement terminates.
    pub adv_task: BoxFuture<'static, ()>,
    /// The relay task used to process incoming connection requests from the upstream
    /// server.
    pub relay_task: fasync::Task<()>,
}

/// The current advertisement status of the `ProfileRegistrar`.
enum AdvertiseStatus {
    /// Currently advertising with a set of tasks processing requests.
    Advertising(AdvertiseTasks),
    /// We are not currently advertising.
    NotAdvertising,
}

/// The ProfileRegistrar handles requests over the `fidl.fuchsia.bluetooth.bredr.Profile` service.
/// Clients can advertise, search, and connect to services.
/// The ProfileRegistrar can be thought of as a relay for clients using Profile. Clients not
/// requesting RFCOMM services will be relayed directly to the upstream server.
///
/// The ProfileRegistrar manages a single RFCOMM advertisement group. If a new client
/// requests to advertise services, the server will unregister the active advertisement, group
/// together all the services, and re-register.
/// The ProfileRegistrar also manages the `RfcommServer` - which is responsible for handling
/// connections over the RFCOMM PSM.
pub struct ProfileRegistrar {
    /// An upstream provider of the Profile service. Typically provided by bt-host.
    profile_upstream: bredr::ProfileProxy,

    /// The `active_registration` is the current processing task for connection requests
    /// from the upstream server.
    active_registration: AdvertiseStatus,

    /// The currently advertised services.
    registered_services: Services,

    /// Sender used to relay connection requests from the upstream server.
    connection_sender: Option<mpsc::Sender<ConnectionEvent>>,

    /// The RFCOMM server that handles allocating server channels, incoming
    /// l2cap connections, outgoing l2cap connections, and multiplexing channels.
    rfcomm_server: RfcommServer,
}

impl ProfileRegistrar {
    fn new(profile_upstream: bredr::ProfileProxy) -> Self {
        Self {
            profile_upstream,
            active_registration: AdvertiseStatus::NotAdvertising,
            registered_services: Services::new(),
            connection_sender: None,
            rfcomm_server: RfcommServer::new(),
        }
    }

    /// Creates the `ProfileRegistrar` and returns a Task representing the running server.
    pub fn start(
        profile_upstream: bredr::ProfileProxy,
        receiver: mpsc::Receiver<bredr::ProfileRequestStream>,
    ) -> fasync::Task<()> {
        let registrar = ProfileRegistrar::new(profile_upstream);
        let handler_fut = registrar.handle_fidl_requests(receiver);
        fasync::Task::spawn(handler_fut)
    }

    /// Returns true if the requested `new_psms` do not overlap with the currently registered PSMs.
    fn is_disjoint_psms(&self, new_psms: &HashSet<u16>) -> bool {
        self.registered_services.psms().is_disjoint(new_psms)
    }

    /// Unregisters all the active services advertised by this server.
    /// This should be called when the upstream server drops the single service advertisement that
    /// this server manages.
    async fn unregister_all_services(&mut self) {
        self.registered_services = Services::new();
        self.rfcomm_server.free_all_server_channels().await;
    }

    /// Unregisters the group of services identified by `handle`. Re-registers any remaining
    /// services.
    /// This should be called when a profile client decides to stop advertising its services.
    async fn unregister_service(&mut self, handle: ServiceGroupHandle) -> Result<(), Error> {
        if !self.registered_services.contains(handle) {
            return Err(format_err!("Attempt to unregister non-existent service: {:?}", handle));
        }

        // Remove the entry for this client.
        let service_info = self.registered_services.remove(handle);
        self.rfcomm_server.free_server_channels(service_info.allocated_server_channels()).await;

        // Attempt to re-advertise.
        self.refresh_advertisement().await;
        Ok(())
    }

    /// Processes an incoming L2cap connection from the upstream server.
    ///
    /// If the connection PSM is not RFCOMM, relays directly to the client.
    ///
    /// Returns an error if the `protocol` is invalidly formatted, or if the provided
    /// PSM is not represented by a client of the `ProfileRegistrar`.
    fn handle_incoming_l2cap_connection(
        &mut self,
        peer_id: PeerId,
        channel: bredr::Channel,
        protocol: Vec<bredr::ProtocolDescriptor>,
    ) -> Result<(), Error> {
        let local = protocol.iter().map(|p| p.into()).collect();
        match psm_from_protocol(&local).ok_or(format_err!("No PSM provided"))? {
            bredr::PSM_RFCOMM => {
                self.rfcomm_server.new_l2cap_connection(peer_id, channel.try_into()?)
            }
            psm => {
                match self.registered_services.iter().find(|(_, client)| client.contains_psm(psm)) {
                    Some((_, client)) => client.relay_connected(peer_id.into(), channel, protocol),
                    None => {
                        return Err(format_err!(
                            "Connection request for non-advertised PSM {:?}",
                            psm
                        ))
                    }
                }
            }
        }
    }

    /// Processes an outgoing L2Cap connection initiated by a client of the ProfileRegistrar.
    ///
    /// Returns an error if the connection request fails.
    async fn handle_outgoing_l2cap_connection(
        &mut self,
        peer_id: PeerId,
        mut connection: bredr::ConnectParameters,
    ) -> Result<bredr::Channel, ErrorCode> {
        // If the provided `connection` is for a non-RFCOMM PSM, simply forward the outbound
        // connection to the upstream Profile service.
        // Otherwise, route to the RFCOMM server.
        match &connection {
            bredr::ConnectParameters::L2cap { .. } => self
                .profile_upstream
                .connect(&mut peer_id.into(), &mut connection)
                .await
                .unwrap_or_else(|_fidl_error| Err(ErrorCode::Failed)),
            bredr::ConnectParameters::Rfcomm { .. } => {
                // TODO(fxbug.dev/49073): Route to RfcommServer and implement RFCOMM functionality.
                Err(ErrorCode::NotSupported)
            }
        }
    }

    /// Advertises `params` to the provided `profile_upstream`.
    ///
    /// Returns a Future for the advertisement; this future should be polled in order to detect
    /// when the advertisement has finished.
    fn advertise(
        profile_upstream: bredr::ProfileProxy,
        params: AdvertiseParams,
        connect_client: ClientEnd<bredr::ConnectionReceiverMarker>,
    ) -> impl Future<Output = ()> {
        let fidl_services = params
            .services
            .iter()
            .map(bredr::ServiceDefinition::try_from)
            .collect_results()
            .unwrap();
        profile_upstream
            .advertise(
                &mut fidl_services.into_iter(),
                (&params.parameters).try_into().unwrap(),
                connect_client,
            )
            .map(|_| ())
    }

    /// Processes requests from the ConnectionReceiver stream and relays to the `sender`.
    async fn connection_request_relay(
        mut connect_requests: bredr::ConnectionReceiverRequestStream,
        mut sender: mpsc::Sender<ConnectionEvent>,
    ) {
        while let Some(connect_request) = connect_requests.next().await {
            match connect_request {
                Ok(request) => {
                    let _ = sender.send(ConnectionEvent::Request(request)).await;
                }
                Err(e) => info!("Connection request error: {:?}", e),
            }
        }
        // The upstream server has dropped the ConnectionReceiver. Let the
        // receiver know that the advertisement has been canceled.
        let _ = sender.send(ConnectionEvent::AdvertisementCanceled).await;
    }

    /// Attempts to build and advertise services from `self.registered_services`.
    async fn refresh_advertisement(&mut self) {
        let status =
            std::mem::replace(&mut self.active_registration, AdvertiseStatus::NotAdvertising);
        match status {
            AdvertiseStatus::Advertising(AdvertiseTasks { adv_task, relay_task }) => {
                // If we are currently advertising, drop the stream processing task to unregister
                // the services. Wait for the advertisement to resolve before attempting to
                // re-advertise.
                drop(relay_task);
                let _ = adv_task.await;
                trace!("Finished waiting for unregistration");
            }
            AdvertiseStatus::NotAdvertising => {}
        }

        // We are ready to advertise. Attempt to build the advertisement parameters, and create
        // and save two tasks that 1) Make the Advertise request and wait for termination and
        // 2) Process incoming requests from the upstream server.
        if let Some(params) = self.registered_services.build_registration() {
            trace!("Advertising from registered services: {:?}", params);
            let (connect_client, connect_requests) =
                create_request_stream::<bredr::ConnectionReceiverMarker>().unwrap();
            // Spawn a task to advertise `params`.
            let adv_fut =
                ProfileRegistrar::advertise(self.profile_upstream.clone(), params, connect_client);
            let adv_task = adv_fut.boxed();
            // Spawn a task to handle incoming L2CAP connections.
            let relay_task = fasync::Task::spawn(ProfileRegistrar::connection_request_relay(
                connect_requests,
                self.connection_sender.clone().unwrap(),
            ));

            self.active_registration =
                AdvertiseStatus::Advertising(AdvertiseTasks { adv_task, relay_task });
        }
    }

    /// Handles an incoming request to advertise a group of `services`.
    ///
    /// At least one service in `services` must request RFCOMM.
    ///
    /// The RFCOMM-requesting services are assigned ServerChannels. The services are then
    /// registered together with the currently registered services.
    ///
    /// Returns the event stream for the receiver tagged with a unique identifier for the
    /// registered group of services. The event stream should be continuously polled in
    /// order to detect when the client terminates the advertisement.
    async fn add_managed_advertisement(
        &mut self,
        mut services: Vec<ServiceDefinition>,
        parameters: ChannelParameters,
        receiver: bredr::ConnectionReceiverProxy,
        responder: bredr::ProfileAdvertiseResponder,
    ) -> Result<StreamWithEpitaph<bredr::ConnectionReceiverEventStream, ServiceGroupHandle>, Error>
    {
        // Validate that the new PSMs are disjoint because we unregister and re-register as a group.
        let new_psms = psms_from_service_definitions(&services);
        if !self.is_disjoint_psms(&new_psms) {
            let _ = responder.send(&mut Err(ErrorCode::Failed));
            return Err(format_err!("New advertisement requesting pre-allocated PSMs"));
        }

        // Create an entry for this group of services with a unique handle.
        let next_handle =
            self.registered_services.insert(ServiceGroup::new(receiver.clone(), parameters));

        // If the RfcommServer has enough free Server Channels, allocate and update
        // the RFCOMM-requesting services.
        let required_server_channels =
            services.iter().filter(|def| is_rfcomm_service_definition(def)).count();
        if required_server_channels > self.rfcomm_server.available_server_channels().await {
            let _ = responder.send(&mut Err(ErrorCode::Failed));
            return Err(format_err!("RfcommServer not enough free Server Channels"));
        }
        for mut service in services.iter_mut().filter(|def| is_rfcomm_service_definition(def)) {
            let server_channel = self
                .rfcomm_server
                .allocate_server_channel(receiver.clone())
                .await
                .expect("just checked");
            update_svc_def_with_server_channel(&mut service, server_channel)?;
        }

        let service_info = self.registered_services.get_mut(next_handle).expect("just inserted");
        service_info.set_service_defs(services);
        service_info.set_responder(responder);

        // Attempt to re-advertise the updated services.
        self.refresh_advertisement().await;

        Ok(receiver.take_event_stream().with_epitaph(next_handle))
    }

    /// Makes a Profile.Advertise() request upstream, and returns a Future that relays the
    /// result to the `responder` upon termination.
    fn make_advertise_relay(
        &self,
        services: Vec<bredr::ServiceDefinition>,
        parameters: bredr::ChannelParameters,
        receiver: ClientEnd<bredr::ConnectionReceiverMarker>,
        responder: bredr::ProfileAdvertiseResponder,
    ) -> impl Future<Output = ()> {
        let adv_fut =
            self.profile_upstream.advertise(&mut services.into_iter(), parameters, receiver);
        async move {
            let _ = adv_fut
                .await
                .and_then(|mut r| responder.send(&mut r))
                .map_err(|e| trace!("Relayed advertisement terminated: {:?}", e));
        }
    }

    /// Handles a request over the Profile protocol.
    ///
    /// If the request was an advertisement, returns either the event stream associated with
    /// the advertise request, or a future to relay the advertisement request directly upstream.
    async fn handle_profile_request(
        &mut self,
        request: bredr::ProfileRequest,
    ) -> Option<AdvertiseResult> {
        match request {
            bredr::ProfileRequest::Advertise { services, parameters, receiver, responder } => {
                let services_local =
                    services.iter().map(ServiceDefinition::try_from).collect_results().ok()?;
                trace!("Received advertise request: {:?}", services_local);
                if service_definitions_request_rfcomm(&services_local) {
                    let receiver = receiver.into_proxy().ok()?;
                    let parameters = ChannelParameters::try_from(&parameters).ok()?;
                    match self
                        .add_managed_advertisement(services_local, parameters, receiver, responder)
                        .await
                    {
                        Err(e) => error!("Error handling advertise request: {:?}", e),
                        Ok(evt_stream) => return Some(AdvertiseResult::EventStream(evt_stream)),
                    }
                } else {
                    return Some(AdvertiseResult::AdvertiseRelay(
                        self.make_advertise_relay(services, parameters, receiver, responder)
                            .boxed(),
                    ));
                }
            }
            bredr::ProfileRequest::Connect { peer_id, connection, responder, .. } => {
                let mut result =
                    self.handle_outgoing_l2cap_connection(peer_id.into(), connection).await;
                let _ = responder.send(&mut result);
            }
            bredr::ProfileRequest::Search { service_uuid, attr_ids, results, .. } => {
                // Simply forward over the search to the Profile server.
                let _ = self.profile_upstream.search(service_uuid, &attr_ids, results);
            }
        }
        None
    }

    /// Handles incoming connection requests from the processing task of the active service
    /// advertisement.
    ///
    /// There are two relevant cases:
    ///   1) A connection request from the upstream Host Server. The incoming l2cap connection
    ///      must be handled - if PSM_RFCOMM, send to the RfcommServer, otherwise, relay
    ///      directly to the profile client.
    ///   2) An epitaph of the relay task signaling the advertisement has canceled. This is
    ///      usually due to an error in the upstream server. We must clear all of the services.
    async fn handle_connection_request(&mut self, request: ConnectionEvent) -> Result<(), Error> {
        match request {
            ConnectionEvent::Request(request) => {
                let bredr::ConnectionReceiverRequest::Connected {
                    peer_id, channel, protocol, ..
                } = request;
                self.handle_incoming_l2cap_connection(peer_id.into(), channel, protocol)
            }
            ConnectionEvent::AdvertisementCanceled => {
                // The upstream server unexpectedly dropped the advertisement. We must clean up
                // all of the state.
                self.unregister_all_services().await;
                Ok(())
            }
        }
    }

    /// `handle_fidl_requests` processes requests/events over several sources.
    /// This is the main "event loop" for the server.
    ///
    /// 1) The server implements the Profile service. Service advertisements, searches, and
    ///    connection requests are processed and handled from any clients of the Profile service.
    /// 2) Connection requests from the upstream server. The active processing task relays
    ///    incoming connection requests for both RFCOMM and non-RFCOMM PSMs.
    /// 3) Termination events from service advertisements of profile clients. The
    ///    event streams of the service advertisements are polled in `client_event_streams`
    ///    and are used to determine when to unregister the aforementioned advertisements.
    /// 4) Profile.Advertise() requests that have been relayed directly to the upstream server
    ///    must be continuously polled to relay the response after termination.
    pub async fn handle_fidl_requests(
        mut self,
        mut profile_request_streams: mpsc::Receiver<bredr::ProfileRequestStream>,
    ) {
        // Collection for bredr.Profile requests.
        let mut profile_requests = futures::stream::SelectAll::new();

        // Internal channel used to relay requests over the `ConnectionReceiver` protocol.
        let (connection_sender, mut connection_receiver) = mpsc::channel(0);
        self.connection_sender = Some(connection_sender);

        // Collection of all client service advertisement event streams. Used to determine
        // when the client has stopped advertising its services.
        let mut client_event_streams = futures::stream::SelectAll::new();

        // Collection of futures of Profile.Advertise requests that have been relayed directly
        // upstream. This is polled continuously to relay the response when the advertisement
        // terminates.
        let mut advertise_futures: FuturesUnordered<BoxFuture<'static, ()>> =
            FuturesUnordered::new();

        loop {
            select! {
                request_stream = profile_request_streams.select_next_some() => {
                    profile_requests.push(request_stream);
                }
                profile_request = profile_requests.select_next_some() => {
                    let profile_request = match profile_request {
                        Ok(request) => request,
                        Err(e) => {
                            info!("Error from Profile request: {:?}", e);
                            continue;
                        }
                    };
                    match self.handle_profile_request(profile_request).await {
                        Some(AdvertiseResult::EventStream(evt_stream)) => client_event_streams.push(evt_stream),
                        Some(AdvertiseResult::AdvertiseRelay(fut)) => advertise_futures.push(fut),
                        _ => {},
                    }
                }
                connection_request = connection_receiver.select_next_some() => {
                    if let Err(e) = self.handle_connection_request(connection_request).await {
                        error!("Error processing incoming l2cap connection request: {:?}", e);
                    }
                }
                service_event = client_event_streams.next() => {
                    if let Some(StreamItem::Epitaph(service_id)) = service_event {
                        // The event stream of the advertisement has been exhausted.
                        // Unregister the service from the ProfileRegistrar.
                        info!("Client {:?} unregistered service advertisement", service_id);
                        if let Err(e) = self.unregister_service(service_id).await {
                            error!("Error unregistering service {:?}: {:?}", service_id, e);
                        }
                    }
                }
                _ = advertise_futures.next() => {},
                complete => break,
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::types::tests::{other_service_definition, rfcomm_service_definition};

    use fidl::encoding::Decodable;
    use fidl::endpoints::create_proxy_and_stream;
    use futures::{pin_mut, task::Poll};

    /// Returns true if the provided `service` has an assigned Server Channel.
    fn service_def_has_assigned_server_channel(service: &bredr::ServiceDefinition) -> bool {
        if let Some(protocol) = &service.protocol_descriptor_list {
            for descriptor in protocol {
                if descriptor.protocol == bredr::ProtocolIdentifier::Rfcomm {
                    if descriptor.params.is_empty() {
                        return false;
                    }
                    // The server channel should be the first element.
                    if let bredr::DataElement::Uint8(_) = descriptor.params[0] {
                        return true;
                    }
                }
            }
        }
        false
    }

    /// Creates a Profile::Search request.
    fn generate_search_request(
        exec: &mut fasync::Executor,
    ) -> (bredr::ProfileRequest, bredr::SearchResultsRequestStream) {
        let (c, mut s) = create_proxy_and_stream::<bredr::ProfileMarker>().unwrap();
        let (results, server) = create_request_stream::<bredr::SearchResultsMarker>().unwrap();

        assert!(c.search(bredr::ServiceClassProfileIdentifier::AudioSink, &[], results).is_ok());

        match exec.run_until_stalled(&mut s.next()) {
            Poll::Ready(Some(Ok(x))) => (x, server),
            x => panic!("Expected ProfileRequest but got: {:?}", x),
        }
    }

    /// Creates a Profile::Advertise request.
    /// Returns the associated request stream, and the Advertise request as a Future.
    fn make_advertise_request(
        client: &bredr::ProfileProxy,
        services: Vec<bredr::ServiceDefinition>,
    ) -> (
        bredr::ConnectionReceiverRequestStream,
        impl Future<Output = Result<Result<(), ErrorCode>, fidl::Error>>,
    ) {
        let (connection, connection_stream) =
            create_request_stream::<bredr::ConnectionReceiverMarker>().unwrap();
        let adv_fut = client.advertise(
            &mut services.into_iter(),
            bredr::ChannelParameters::new_empty(),
            connection,
        );
        (connection_stream, adv_fut)
    }

    fn new_client(
        exec: &mut fasync::Executor,
        mut profile_sender: mpsc::Sender<bredr::ProfileRequestStream>,
    ) -> bredr::ProfileProxy {
        let (profile_client, profile_server) =
            create_proxy_and_stream::<bredr::ProfileMarker>().unwrap();
        let send_fut = profile_sender.send(profile_server);
        pin_mut!(send_fut);
        let _ = exec.run_until_stalled(&mut send_fut);
        profile_client
    }

    /// Returns the `ProfileRegistrar.handle_fidl_requests()` Future. This consumes
    /// the `server`.
    fn setup_handler_fut(
        server: ProfileRegistrar,
    ) -> (mpsc::Sender<bredr::ProfileRequestStream>, impl Future<Output = ()>) {
        let (profile_sender, profile_receiver) = mpsc::channel(0);
        let handler_fut = server.handle_fidl_requests(profile_receiver);
        (profile_sender, handler_fut)
    }

    /// Creates the ProfileRegistrar with the upstream Profile service.
    fn setup_server() -> (fasync::Executor, ProfileRegistrar, bredr::ProfileRequestStream) {
        let exec = fasync::Executor::new().unwrap();
        let (client, server) = create_proxy_and_stream::<bredr::ProfileMarker>().unwrap();
        let profile_server = ProfileRegistrar::new(client);
        (exec, profile_server, server)
    }

    /// Exercises a service advertisement with an empty set of services.
    /// In practice, the upstream Host Server will reject this request but the RFCOMM
    /// server will still classify the request as non-RFCOMM only, and relay directly
    /// to the Profile Server.
    /// This test validates that the parameters are relayed directly to the Profile Server. Also
    /// validates that when the upstream Advertise call resolves, the result is relayed to the
    /// client.
    #[test]
    fn test_handle_empty_advertise_request() {
        let (mut exec, server, mut upstream_requests) = setup_server();

        let (profile_sender, handler_fut) = setup_handler_fut(server);
        pin_mut!(handler_fut);
        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());

        // A new client connects to bt-rfcomm.cmx.
        let client = {
            let client = new_client(&mut exec, profile_sender.clone());
            let _ = exec.run_until_stalled(&mut handler_fut);
            client
        };

        // Client decides to advertise empty services.
        let services = vec![];
        let (_connection_stream, adv_fut) = make_advertise_request(&client, services);
        pin_mut!(adv_fut);
        assert!(exec.run_until_stalled(&mut adv_fut).is_pending());

        let _ = exec.run_until_stalled(&mut handler_fut);

        // The advertisement request should be relayed directly upstream.
        let responder = match exec.run_until_stalled(&mut upstream_requests.next()) {
            Poll::Ready(Some(Ok(bredr::ProfileRequest::Advertise { responder, .. }))) => responder,
            x => panic!("Expected advertise request, got: {:?}", x),
        };

        // Upstream decides to resolve the Advertise call.
        let _ = responder.send(&mut Ok(()));

        let _ = exec.run_until_stalled(&mut handler_fut);
        // Client should be notified, and it's advertisement should terminate.
        assert!(exec.run_until_stalled(&mut adv_fut).is_ready());
    }

    /// Exercises a service advertisement with no RFCOMM services.
    /// The ProfileRegistrar server should classify the request as non-RFCOMM only, and relay
    /// directly to the upstream Profile Server.
    #[test]
    fn test_handle_advertise_request_with_no_rfcomm() -> Result<(), Error> {
        let (mut exec, server, mut upstream_requests) = setup_server();

        let (profile_sender, handler_fut) = setup_handler_fut(server);
        pin_mut!(handler_fut);
        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());

        // A new client connects to bt-rfcomm.cmx.
        let client = {
            let client = new_client(&mut exec, profile_sender.clone());
            let _ = exec.run_until_stalled(&mut handler_fut);
            client
        };

        // Client decides to advertise.
        let services = vec![bredr::ServiceDefinition::try_from(&other_service_definition(1))?];
        let (_connection_stream, adv_fut) = make_advertise_request(&client, services);
        pin_mut!(adv_fut);
        assert!(exec.run_until_stalled(&mut adv_fut).is_pending());

        let _ = exec.run_until_stalled(&mut handler_fut);

        // The advertisement request should be relayed directly upstream.
        match exec.run_until_stalled(&mut upstream_requests.next()) {
            Poll::Ready(Some(Ok(bredr::ProfileRequest::Advertise { .. }))) => {}
            x => panic!("Expected advertise request, got: {:?}", x),
        };
        Ok(())
    }

    /// Service advertisement with only RFCOMM services. The services should be assigned
    /// Server Channels and be relayed upstream.
    #[test]
    fn test_handle_advertise_request_with_only_rfcomm() -> Result<(), Error> {
        let (mut exec, server, mut upstream_requests) = setup_server();

        let (profile_sender, handler_fut) = setup_handler_fut(server);
        pin_mut!(handler_fut);
        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());

        // A new client connects to bt-rfcomm.cmx.
        let client = {
            let client = new_client(&mut exec, profile_sender.clone());
            let _ = exec.run_until_stalled(&mut handler_fut);
            client
        };

        // Client decides to advertise.
        let services = vec![bredr::ServiceDefinition::try_from(&rfcomm_service_definition(None))?];
        let (_connection_stream, adv_fut) = make_advertise_request(&client, services);
        pin_mut!(adv_fut);
        assert!(exec.run_until_stalled(&mut adv_fut).is_pending());

        let _ = exec.run_until_stalled(&mut handler_fut);

        // Upstream should receive Advertise request for a service with an assigned server channel.
        match exec.run_until_stalled(&mut upstream_requests.next()) {
            Poll::Ready(Some(Ok(bredr::ProfileRequest::Advertise { services, .. }))) => {
                assert_eq!(services.len(), 1);
                assert!(service_def_has_assigned_server_channel(&services[0]));
            }
            x => panic!("Expected advertise request, got: {:?}", x),
        };
        Ok(())
    }

    /// Service advertisement with both RFCOMM and non-RFCOMM services. Only the RFCOMM
    /// services should be assigned Server Channels, and the group should be registered upstream.
    #[test]
    fn test_handle_advertise_request_with_all_services() -> Result<(), Error> {
        let (mut exec, server, mut upstream_requests) = setup_server();

        let (profile_sender, handler_fut) = setup_handler_fut(server);
        pin_mut!(handler_fut);
        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());

        // A new client connects to bt-rfcomm.cmx.
        let client = {
            let client = new_client(&mut exec, profile_sender.clone());
            let _ = exec.run_until_stalled(&mut handler_fut);
            client
        };

        // Client decides to advertise.
        let services = vec![
            bredr::ServiceDefinition::try_from(&rfcomm_service_definition(None))?,
            bredr::ServiceDefinition::try_from(&other_service_definition(10))?,
            bredr::ServiceDefinition::try_from(&rfcomm_service_definition(None))?,
        ];
        let n = services.len();
        let (_connection_stream, adv_fut) = make_advertise_request(&client, services);
        pin_mut!(adv_fut);
        assert!(exec.run_until_stalled(&mut adv_fut).is_pending());

        let _ = exec.run_until_stalled(&mut handler_fut);

        // Expect an advertise request with all n services - validate that the RFCOMM services
        // have assigned server channels.
        match exec.run_until_stalled(&mut upstream_requests.next()) {
            Poll::Ready(Some(Ok(bredr::ProfileRequest::Advertise { services, .. }))) => {
                assert_eq!(services.len(), n);
                let res = services
                    .into_iter()
                    .filter(|service| {
                        is_rfcomm_service_definition(&ServiceDefinition::try_from(service).unwrap())
                    })
                    .map(|service| service_def_has_assigned_server_channel(&service))
                    .fold(true, |acc, elt| acc || elt);
                assert!(res);
            }
            x => panic!("Expected advertise request, got: {:?}", x),
        }
        Ok(())
    }

    /// Tests handling two advertise requests with overlapping PSMs. The first one
    /// should succeed and be relayed upstream. The second one should fail since it
    /// is requesting already-allocated PSMs - the responder should be notified of the error.
    #[test]
    fn test_handle_advertise_requests_same_psm() -> Result<(), Error> {
        let (mut exec, server, mut upstream_requests) = setup_server();

        let (profile_sender, handler_fut) = setup_handler_fut(server);
        pin_mut!(handler_fut);
        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());

        // A new client connects to bt-rfcomm.cmx.
        let client1 = {
            let client = new_client(&mut exec, profile_sender.clone());
            let _ = exec.run_until_stalled(&mut handler_fut);
            client
        };

        // Client decides to advertise.
        let psm = 10;
        let services1 = vec![
            bredr::ServiceDefinition::try_from(&other_service_definition(psm))?,
            bredr::ServiceDefinition::try_from(&rfcomm_service_definition(None))?,
        ];
        let (_connection_stream1, adv_fut1) = make_advertise_request(&client1, services1);
        pin_mut!(adv_fut1);
        assert!(exec.run_until_stalled(&mut adv_fut1).is_pending());

        let _ = exec.run_until_stalled(&mut handler_fut);

        // Save the Advertise parameters.
        let _adv_req1 = match exec.run_until_stalled(&mut upstream_requests.next()) {
            Poll::Ready(Some(Ok(request))) => request,
            x => panic!("Expected advertise request, got: {:?}", x),
        };

        // A different client connects to bt-rfcomm.cmx. It decides to try to advertise over same
        // PSM.
        let client2 = {
            let client = new_client(&mut exec, profile_sender.clone());
            let _ = exec.run_until_stalled(&mut handler_fut);
            client
        };
        let services2 = vec![
            bredr::ServiceDefinition::try_from(&other_service_definition(psm))?,
            bredr::ServiceDefinition::try_from(&rfcomm_service_definition(None))?,
        ];
        let (_connection_stream2, adv_fut2) = make_advertise_request(&client2, services2);
        pin_mut!(adv_fut2);
        assert!(exec.run_until_stalled(&mut adv_fut2).is_pending());

        let _ = exec.run_until_stalled(&mut handler_fut);

        // Advertisement 1 is OK. Advertisement 2 should resolve immediately with an ErrorCode.
        assert!(exec.run_until_stalled(&mut adv_fut1).is_pending());
        match exec.run_until_stalled(&mut adv_fut2) {
            Poll::Ready(Ok(Err(e))) => assert_eq!(e, ErrorCode::Failed),
            x => panic!("Expected Ready with ErrorCode but got {:?}", x),
        }

        Ok(())
    }

    /// Tests that independent service advertisements from multiple clients are correctly
    /// grouped together and re-registered.
    #[test]
    fn test_handle_multiple_service_advertisements() -> Result<(), Error> {
        let (mut exec, server, mut upstream_requests) = setup_server();

        let (profile_sender, handler_fut) = setup_handler_fut(server);
        pin_mut!(handler_fut);
        assert!(exec.run_until_stalled(&mut handler_fut).is_pending());

        // A new client connects to bt-rfcomm.cmx.
        let client1 = {
            let client = new_client(&mut exec, profile_sender.clone());
            let _ = exec.run_until_stalled(&mut handler_fut);
            client
        };

        // Client decides to advertise.
        let psm1 = 10;
        let services1 = vec![
            bredr::ServiceDefinition::try_from(&other_service_definition(psm1))?,
            bredr::ServiceDefinition::try_from(&rfcomm_service_definition(None))?,
        ];
        let n1 = services1.len();
        let (_connection_stream1, adv_fut1) = make_advertise_request(&client1, services1);
        pin_mut!(adv_fut1);
        assert!(exec.run_until_stalled(&mut adv_fut1).is_pending());

        let _ = exec.run_until_stalled(&mut handler_fut);

        // First advertisement request relayed upstream.
        let (_receiver1, responder1) = match exec.run_until_stalled(&mut upstream_requests.next()) {
            Poll::Ready(Some(Ok(bredr::ProfileRequest::Advertise {
                receiver, responder, ..
            }))) => (receiver, responder),
            x => panic!("Expected advertise request, got: {:?}", x),
        };

        // A different client connects to bt-rfcomm.cmx. It decides to try to advertise over same
        // PSM.
        let client2 = {
            let client = new_client(&mut exec, profile_sender.clone());
            let _ = exec.run_until_stalled(&mut handler_fut);
            client
        };
        // Client 2 decides to advertise three services.
        let psm2 = 15;
        let n2 = 3;
        let services2 = vec![
            bredr::ServiceDefinition::try_from(&other_service_definition(psm2))?,
            bredr::ServiceDefinition::try_from(&rfcomm_service_definition(None))?,
            bredr::ServiceDefinition::try_from(&rfcomm_service_definition(None))?,
        ];
        let (_connection_stream2, adv_fut2) = make_advertise_request(&client2, services2);
        pin_mut!(adv_fut2);
        assert!(exec.run_until_stalled(&mut adv_fut2).is_pending());

        let _ = exec.run_until_stalled(&mut handler_fut);

        // We expect ProfileRegistrar to unregister the current active advertisement. Respond to
        // the unregister request by responding over the responder.
        let _ = responder1.send(&mut Ok(()));
        let _ = exec.run_until_stalled(&mut handler_fut);
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());

        // We expect a new advertisement upstream.
        let (_receiver2, _responder2) = match exec.run_until_stalled(&mut upstream_requests.next())
        {
            Poll::Ready(Some(Ok(bredr::ProfileRequest::Advertise {
                services,
                receiver,
                responder,
                ..
            }))) => {
                assert_eq!(services.len(), n1 + n2);
                (receiver, responder)
            }
            x => panic!("Expected advertise request, got: {:?}", x),
        };
        assert!(exec.run_until_stalled(&mut adv_fut1).is_pending());
        assert!(exec.run_until_stalled(&mut adv_fut2).is_pending());

        Ok(())
    }

    /// This test validates that client Search requests are relayed directly upstream.
    #[test]
    fn test_handle_search_request() {
        let (mut exec, mut server, mut profile_requests) = setup_server();

        let (search_request, _stream) = generate_search_request(&mut exec);

        let handle_fut = server.handle_profile_request(search_request);
        pin_mut!(handle_fut);

        assert!(exec.run_until_stalled(&mut handle_fut).is_ready());

        // The search request should be relayed directly to the Profile Server.
        match exec.run_until_stalled(&mut profile_requests.next()) {
            Poll::Ready(Some(Ok(bredr::ProfileRequest::Search { .. }))) => {}
            x => panic!("Expected search request, got: {:?}", x),
        }
    }

    /// This tests validates that 1) The call to Profile.Advertise correctly registers upstream and
    /// 2) When the call resolves, the Future resolves.
    #[test]
    fn test_advertise_relay() {
        let mut exec = fasync::Executor::new().unwrap();
        let (upstream, mut upstream_server) =
            create_proxy_and_stream::<bredr::ProfileMarker>().unwrap();
        let (connect_client, _connect_requests) =
            create_request_stream::<bredr::ConnectionReceiverMarker>().unwrap();
        let params = AdvertiseParams { services: vec![], parameters: ChannelParameters::default() };

        let advertise_fut = ProfileRegistrar::advertise(upstream, params, connect_client);
        pin_mut!(advertise_fut);
        assert!(exec.run_until_stalled(&mut advertise_fut).is_pending());

        let (_connection_receiver, responder) = match exec
            .run_until_stalled(&mut upstream_server.next())
        {
            Poll::Ready(Some(Ok(bredr::ProfileRequest::Advertise {
                receiver, responder, ..
            }))) => (receiver, responder),
            x => panic!("Expected Advertise request but got: {:?}", x),
        };

        assert!(exec.run_until_stalled(&mut advertise_fut).is_pending());

        // Upstream server decides to terminate advertisement - we expect the Future to finish.
        let _ = responder.send(&mut Ok(()));
        assert!(exec.run_until_stalled(&mut advertise_fut).is_ready());
    }

    /// This test validates that incoming connection requests are correctly relayed
    /// to the Sender of the connection task.
    #[test]
    fn test_connection_request_relay() {
        let mut exec = fasync::Executor::new().unwrap();

        let (connect_client, connect_requests) =
            create_proxy_and_stream::<bredr::ConnectionReceiverMarker>().unwrap();
        let (sender, mut receiver) = mpsc::channel(0);

        let relay_fut = ProfileRegistrar::connection_request_relay(connect_requests, sender);
        pin_mut!(relay_fut);

        let receiver_fut = receiver.next();
        pin_mut!(receiver_fut);

        // The task should still be active and no messages sent.
        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());
        assert!(exec.run_until_stalled(&mut receiver_fut).is_pending());

        // Upstream server gives us a connection.
        let id = PeerId(123);
        let mut protocol = vec![];
        assert!(connect_client
            .connected(&mut id.into(), bredr::Channel::new_empty(), &mut protocol.iter_mut())
            .is_ok());

        // Run the relay fut - should still be running.
        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());

        // The relay should've sent the ConnectionEvent to the receiver.
        match exec.run_until_stalled(&mut receiver_fut) {
            Poll::Ready(Some(ConnectionEvent::Request(
                bredr::ConnectionReceiverRequest::Connected { .. },
            ))) => {}
            x => panic!("Expected connection request but got {:?}", x),
        }

        // Upstream drops ConnectionReceiver client for some reason.
        drop(connect_client);

        // The relay should notify the sender that the stream has terminated (i.e relay Canceled).
        assert!(exec.run_until_stalled(&mut relay_fut).is_pending());
        match exec.run_until_stalled(&mut receiver_fut) {
            Poll::Ready(Some(ConnectionEvent::AdvertisementCanceled)) => {}
            x => panic!("Expected Canceled but got: {:?}", x),
        }

        // Relay should be finished since the channel is closed.
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        assert!(exec.run_until_stalled(&mut relay_fut).is_ready());
    }
}
