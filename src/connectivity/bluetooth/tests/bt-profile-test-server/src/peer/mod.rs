// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth_bredr as bredr,
    fidl_fuchsia_sys::ComponentControllerEvent,
    fuchsia_bluetooth::{
        detachable_map::DetachableMap,
        types::{Channel, PeerId},
    },
    fuchsia_component::{client, client::App, server::NestedEnvironment},
    fuchsia_syslog::fx_log_info,
    futures::{
        stream::{StreamExt, TryStreamExt},
        Future, Stream,
    },
    parking_lot::RwLock,
    std::{
        collections::{HashMap, HashSet},
        convert::TryInto,
        sync::Arc,
    },
};

pub mod search;
pub mod service;

use self::search::SearchSet;
use self::service::{RegistrationHandle, ServiceSet};
use crate::profile::{build_l2cap_descriptor, parse_service_definitions};
use crate::types::{Psm, ServiceRecord};

/// Default SDU size the peer is capable of accepting. This is chosen as the minimum
/// size documented in [`fuchsia.bluetooth.bredr.Channel`].
const DEFAULT_TX_SDU_SIZE: usize = 48;

/// The unique identifier for a launched profile. This is used for internal bookkeeping
/// and has no meaning outside of this context.
#[derive(Copy, Clone, Debug, Hash, Eq, PartialEq)]
pub struct ProfileHandle(u64);

/// The status events corresponding to a launched component's lifetime.
#[derive(Debug, Clone)]
pub enum ComponentStatus {
    /// The component has been successfully launched, and output directory is ready.
    DirectoryReady,

    /// The component has terminated.
    Terminated,
}

/// The primary object that represents a peer in the piconet.
/// `MockPeer` facilitates the registering, advertising, and connecting of services
/// for a peer.
/// Each `MockPeer` contains it's own NestedEnvironment, allowing for the sandboxed
/// launching of profiles.
pub struct MockPeer {
    /// The unique identifier for this peer.
    id: PeerId,

    /// The nested environment for this peer. Used for the sandboxed launching of profiles.
    env: NestedEnvironment,

    /// The PeerObserver relay for this peer. This is used to send updates about this peer.
    /// If not set, no updates will be relayed.
    observer: Option<bredr::PeerObserverProxy>,

    /// The next available ProfileHandle.
    next_profile_handle: ProfileHandle,

    /// Information about the profiles that have been launched by this peer.
    launched_profiles: DetachableMap<ProfileHandle, (String, App)>,

    /// Manages the active searches for this peer.
    search_mgr: Arc<RwLock<SearchSet>>,

    /// The ServiceSet handles the registration and unregistration of services
    /// that this peer provides.
    service_mgr: Arc<RwLock<ServiceSet>>,

    /// Outstanding advertised services and their connection receivers.
    services: DetachableMap<RegistrationHandle, bredr::ConnectionReceiverProxy>,
}

impl MockPeer {
    pub fn new(
        id: PeerId,
        env: NestedEnvironment,
        observer: Option<bredr::PeerObserverProxy>,
    ) -> Self {
        // TODO(55462): If provided, take event stream of `observer` and listen for close.
        Self {
            id,
            env,
            observer,
            next_profile_handle: ProfileHandle(1),
            launched_profiles: DetachableMap::new(),
            search_mgr: Arc::new(RwLock::new(SearchSet::new())),
            service_mgr: Arc::new(RwLock::new(ServiceSet::new(id))),
            services: DetachableMap::new(),
        }
    }

    pub fn peer_id(&self) -> PeerId {
        self.id
    }

    pub fn env(&self) -> &NestedEnvironment {
        &self.env
    }

    /// Returns the set of active searches, identified by their Service Class ID.
    pub fn get_active_searches(&self) -> HashSet<bredr::ServiceClassProfileIdentifier> {
        self.search_mgr.read().get_active_searches()
    }

    /// Returns the advertised services of this peer that conform to the provided `ids`.
    pub fn get_advertised_services(
        &self,
        ids: &HashSet<bredr::ServiceClassProfileIdentifier>,
    ) -> HashMap<bredr::ServiceClassProfileIdentifier, Vec<ServiceRecord>> {
        self.service_mgr.read().get_service_records(ids)
    }

    /// Returns the next available `ProfileHandle`.
    fn get_next_profile_handle(&mut self) -> ProfileHandle {
        let next_profile_handle = self.next_profile_handle;
        self.next_profile_handle = ProfileHandle(next_profile_handle.0 + 1);
        next_profile_handle
    }

    /// Attempts to launch the profile specified by `profile_url`.
    ///
    /// Returns a stream that monitors component state. The returned stream _must_ be polled
    /// in order to complete component launching. Furthermore, it should also be polled to
    /// detect component termination - state will be cleaned up thereafter.
    pub fn launch_profile(
        &mut self,
        profile_url: String,
    ) -> Result<impl Stream<Item = Result<ComponentStatus, fidl::Error>>, Error> {
        let next_profile_handle = self.get_next_profile_handle();

        // Launch the component and grab the event stream.
        let app = client::launch(self.env.launcher(), profile_url.clone(), None)?;
        let component_stream = app.controller().take_event_stream();

        let entry = self.launched_profiles.lazy_entry(&next_profile_handle);
        let detached_component = match entry.try_insert((profile_url.clone(), app)) {
            Ok(component) => component,
            Err(_) => return Err(format_err!("Launched component already exists")),
        };

        // The returned `component_stream` processes events from the launched component's
        // event stream.
        let peer_id = self.id.clone();
        let handle = next_profile_handle.clone();
        let observer = self.observer.clone();
        let component_stream = component_stream.map_ok(move |event| {
            let detached = detached_component.clone();
            let url_clone = profile_url.clone();
            let observer_clone = observer.clone();
            match event {
                ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
                    fx_log_info!(
                        "Peer {:?}. Component {:?}, terminated. Code: {}. Reason: {:?}",
                        peer_id,
                        handle,
                        return_code,
                        termination_reason
                    );
                    detached.detach();
                    observer_clone.map(|o| Self::relay_terminated(&o, url_clone));
                    ComponentStatus::Terminated
                }
                ComponentControllerEvent::OnDirectoryReady { .. } => {
                    ComponentStatus::DirectoryReady
                }
            }
        });
        Ok(component_stream)
    }

    /// Notifies the `observer` with the ServiceFound update from the ServiceRecord.
    fn relay_service_found(observer: &bredr::PeerObserverProxy, record: ServiceRecord) {
        let mut response = record.to_service_found_response().unwrap();
        let mut protocol = response.protocol.as_mut().map(|v| v.iter_mut());
        let protocol = protocol
            .as_mut()
            .map(|v| -> &mut dyn ExactSizeIterator<Item = &mut bredr::ProtocolDescriptor> { v });
        let _ = observer.service_found(
            &mut response.id.into(),
            protocol,
            &mut response.attributes.iter_mut(),
        );
    }

    /// Notifies the `observer` with the URL of the terminated profile.
    fn relay_terminated(observer: &bredr::PeerObserverProxy, profile_url: String) {
        let _ = observer.component_terminated(&profile_url);
    }

    /// Notifies the `observer` with the connection on `psm` established by peer `other`.
    fn relay_connected(observer: &bredr::PeerObserverProxy, other: PeerId, psm: Psm) {
        let mut protocol = build_l2cap_descriptor(psm);
        let _ = observer.peer_connected(&mut other.into(), &mut protocol.iter_mut());
    }

    /// Notifies all the searches for service class `id` with the published
    /// services in `services`.
    /// The relevant services of a peer are provided in `services`.
    /// If the ServiceFound response was successful, relays the data to the PeerObserver.
    pub fn notify_searches(
        &mut self,
        id: &bredr::ServiceClassProfileIdentifier,
        services: Vec<ServiceRecord>,
    ) {
        let proxy = self.observer.clone();
        let mut w_search_mgr = self.search_mgr.write();
        for service in &services {
            let notifs = w_search_mgr.notify_searches(id, service.clone());
            // Relay the number of searches notified to the observer.
            for _ in 0..notifs {
                proxy.as_ref().map(|o| Self::relay_service_found(o, service.clone()));
            }
        }
    }

    /// Attempts to register the services, as a group, specified by `services`.
    ///
    /// Returns 1) The ServiceClassProfileIds of the registered services - this is used to
    /// speed up the matching process for any outstanding searches. 2) A future that should
    /// be polled in order to remove the advertisement when `proxy` is closed.
    /// Returns an Error if any service in the provided ServiceDefinitions are invalid, or if
    /// registration with the ServiceSet failed.
    pub fn new_advertisement(
        &mut self,
        services: Vec<bredr::ServiceDefinition>,
        proxy: bredr::ConnectionReceiverProxy,
    ) -> Result<(HashSet<bredr::ServiceClassProfileIdentifier>, impl Future<Output = ()>), Error>
    {
        let service_records = parse_service_definitions(services)?;
        let registration_handle = self
            .service_mgr
            .write()
            .register_service(service_records)
            .ok_or(format_err!("Registration of services failed"))?;

        // Take the ConnectionReceiver event stream to monitor the channel for closure.
        let service_event_stream = proxy.take_event_stream();

        self.services.insert(registration_handle, proxy);
        let ids = self
            .service_mgr
            .read()
            .get_service_ids_for_registration_handle(&registration_handle)
            .unwrap_or(&HashSet::new())
            .clone();

        let peer_id = self.id.clone();
        let reg_handle_clone = registration_handle.clone();
        let detached_service = self.services.get(&registration_handle).expect("just added");
        let service_mgr_clone = self.service_mgr.clone();
        let closed_fut = async move {
            let _ = service_event_stream.map(|_| ()).collect::<()>().await;
            fx_log_info!("Peer {} unregistering service advertisement", peer_id);
            detached_service.detach();
            service_mgr_clone.write().unregister_service(&reg_handle_clone);
        };

        Ok((ids, closed_fut))
    }

    /// Creates a new connection if the requested `psm` is registered by this peer.
    /// Updates the relevant `ConnectionReceiver` with one end of the channel.
    /// Updates the `PeerObserver`, if set.
    ///
    /// `other` is the PeerID of remote peer that is requesting the connection.
    ///
    /// Returns the created Channel if the provided `psm` is valid.
    pub fn new_connection(&self, other: PeerId, psm: Psm) -> Result<bredr::Channel, Error> {
        let reg_handle = self
            .service_mgr
            .read()
            .psm_registered(psm)
            .ok_or(format_err!("PSM {:?} not registered", psm))?;
        let proxy = self
            .services
            .get(&reg_handle)
            .and_then(|p| p.upgrade())
            .ok_or(format_err!("Connection receiver doesn't exist"))?;

        // Build the L2CAP descriptor and notify the ConnectionReceiver.
        let mut protocol = build_l2cap_descriptor(psm);
        let (channel1, channel2) = Channel::create_with_max_tx(DEFAULT_TX_SDU_SIZE);
        proxy.connected(&mut other.into(), channel2.try_into()?, &mut protocol.iter_mut())?;

        // Potentially update the observer relay with the connection information.
        self.observer.as_ref().map(|o| Self::relay_connected(o, other, psm));

        channel1.try_into()
    }

    /// Registers a new search for the provided `service_uuid`.
    /// Returns a future that should be polled in order to remove the search
    /// when `proxy` is closed.
    pub fn new_search(
        &mut self,
        service_uuid: bredr::ServiceClassProfileIdentifier,
        attr_ids: Vec<u16>,
        proxy: bredr::SearchResultsProxy,
    ) -> impl Future<Output = ()> {
        let search_stream = proxy.take_event_stream();

        let search_handle = {
            let mut w_search_mgr = self.search_mgr.write();
            w_search_mgr.add(service_uuid, attr_ids, proxy)
        };

        let peer_id = self.id.clone();
        let uuid = service_uuid.clone();
        let search_mgr_clone = self.search_mgr.clone();
        let closed_fut = async move {
            let _ = search_stream.map(|_| ()).collect::<()>().await;
            if search_mgr_clone.write().remove(search_handle) {
                fx_log_info!("Peer {} unregistering service search {:?}", peer_id, uuid);
            }
        };

        closed_fut
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::peer::service::tests::build_a2dp_service_record;
    use crate::profile::tests::build_a2dp_service_definition;
    use crate::types::RegisteredServiceId;

    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_bredr::*;
    use fidl_fuchsia_sys::EnvironmentOptions;
    use fuchsia_async as fasync;
    use fuchsia_component::{fuchsia_single_component_package_url, server::ServiceFs};
    use futures::{lock::Mutex, pin_mut, task::Poll, StreamExt};
    use std::sync::Arc;

    /// `TestEnvironment` is used to store the FIDL Client Ends of any requests
    /// over the `fuchsia.bluetooth.bredr.Profile` service. This is because if
    /// the handles are dropped, the launched profile (A2DP Sink, Source, AVRCP) will
    /// terminate with an error.
    /// `TestEnvironment` does not do anything except for store the proxies and provide
    /// a way to drop them (to simulate disconnection).
    struct TestEnvironment {
        search: Vec<bredr::SearchResultsProxy>,
        adv: Vec<bredr::ConnectionReceiverProxy>,
    }

    impl TestEnvironment {
        pub fn new() -> Self {
            Self { search: vec![], adv: vec![] }
        }

        pub fn add_search(&mut self, search: bredr::SearchResultsProxy) {
            self.search.push(search);
        }

        pub fn add_advertisement(&mut self, adv: ConnectionReceiverProxy) {
            self.adv.push(adv);
        }

        pub fn clear_searches(&mut self) {
            self.search.clear();
        }

        pub fn clear_advertisements(&mut self) {
            self.adv.clear();
        }
    }

    /// Accepts requests over the `fuchsia.bluetooth.bredr.Profile` service and stores the search
    /// and advertising handles in the `env`.
    /// All search and advertisement requests over the Profile service will be accepted and stored.
    /// Connection requests aren't required to unit test the successful launching of profiles, so
    /// they are ignored.
    fn handle_profile_requests(mut stream: ProfileRequestStream, env: Arc<Mutex<TestEnvironment>>) {
        fasync::Task::spawn(async move {
            while let Some(request) = stream.next().await {
                if let Ok(req) = request {
                    match req {
                        ProfileRequest::Advertise { receiver, .. } => {
                            let proxy = receiver.into_proxy().unwrap();
                            let mut w_env = env.lock().await;
                            w_env.add_advertisement(proxy);
                        }
                        ProfileRequest::Search { results, .. } => {
                            let proxy = results.into_proxy().unwrap();
                            let mut w_env = env.lock().await;
                            w_env.add_search(proxy);
                        }
                        ProfileRequest::Connect { .. } => {}
                    }
                }
            }
        })
        .detach();
    }

    fn setup_environment(
        id: PeerId,
    ) -> Result<(NestedEnvironment, Arc<Mutex<TestEnvironment>>), Error> {
        let env_variables = Arc::new(Mutex::new(TestEnvironment::new()));
        let env_vars_clone = env_variables.clone();
        let mut service_fs = ServiceFs::new();
        service_fs
            .add_fidl_service(move |stream| handle_profile_requests(stream, env_variables.clone()));
        let env_name = format!("peer_{}", id);
        let options = EnvironmentOptions {
            inherit_parent_services: true,
            use_parent_runners: false,
            kill_on_oom: false,
            delete_storage_on_death: false,
        };
        let env = service_fs.create_nested_environment_with_options(env_name.as_str(), options)?;
        fasync::Task::spawn(service_fs.collect()).detach();

        Ok((env, env_vars_clone))
    }

    /// Creates a MockPeer with the sandboxed NestedEnvironment.
    /// Returns the MockPeer, the relay for updates for the peer, and a TestEnvironment object
    /// to keep alive relevant state.
    fn create_mock_peer(
        id: PeerId,
    ) -> Result<(MockPeer, PeerObserverRequestStream, Arc<Mutex<TestEnvironment>>), Error> {
        let (env, env_vars) = setup_environment(id)?;
        let (proxy, stream) = create_proxy_and_stream::<PeerObserverMarker>().unwrap();
        Ok((MockPeer::new(id, env, Some(proxy)), stream, env_vars))
    }

    /// Builds and registers a search for an `id` with no attributes.
    /// Returns 1) The RequestStream used to process search results and 2) A future
    /// that signals the termination of the service search.
    fn build_and_register_search(
        mock_peer: &mut MockPeer,
        id: ServiceClassProfileIdentifier,
    ) -> (SearchResultsRequestStream, impl Future<Output = ()>) {
        let (client, stream) =
            create_proxy_and_stream::<SearchResultsMarker>().expect("couldn't create endpoints");
        let search_fut = mock_peer.new_search(id, vec![], client);
        (stream, search_fut)
    }

    /// Builds and registers an A2DP Sink service advertisement.
    /// Returns 1) ServerEnd of the ConnectionReceiver that will be used to receive l2cap
    /// connections, 2) A future that signals the termination of the service advertisement
    /// and 3) The ServiceClassProfileIds that were registered.
    fn build_and_register_service(
        mock_peer: &mut MockPeer,
    ) -> (
        ConnectionReceiverRequestStream,
        impl Future<Output = ()>,
        HashSet<ServiceClassProfileIdentifier>,
    ) {
        // Build the A2DP Sink Service Definition.
        let (a2dp_def, _) = build_a2dp_service_definition();
        let mut expected_ids = HashSet::new();
        expected_ids.insert(ServiceClassProfileIdentifier::AudioSink);

        // Register the service.
        let (receiver, stream) = create_proxy_and_stream::<ConnectionReceiverMarker>().unwrap();
        let res = mock_peer.new_advertisement(vec![a2dp_def], receiver);
        assert!(res.is_ok());
        let (svc_ids, adv_fut) = res.unwrap();
        assert_eq!(expected_ids, svc_ids);

        (stream, adv_fut, svc_ids)
    }

    /// Launches the profile specified by `profile_url`.
    fn do_launch_profile(
        exec: &mut fasync::Executor,
        profile_url: String,
        mock_peer: &mut MockPeer,
    ) -> impl Stream<Item = Result<ComponentStatus, fidl::Error>> {
        let launch_res = mock_peer.launch_profile(profile_url.clone());
        let mut component_stream = launch_res.expect("profile should launch");
        match exec.run_singlethreaded(&mut component_stream.next()) {
            Some(Ok(ComponentStatus::DirectoryReady)) => {}
            x => panic!("Expected directory ready but got: {:?}", x),
        }

        component_stream
    }

    /// Expects a ServiceFound call to the `stream`.
    /// Panics if the call doesn't happen.
    fn expect_search_service_found(
        exec: &mut fasync::Executor,
        stream: &mut SearchResultsRequestStream,
    ) {
        let service_found_fut = stream.select_next_some();
        pin_mut!(service_found_fut);

        match exec.run_until_stalled(&mut service_found_fut) {
            Poll::Ready(Ok(SearchResultsRequest::ServiceFound { responder, .. })) => {
                let _ = responder.send();
            }
            x => panic!("Expected ServiceFound request but got: {:?}", x),
        }
    }

    /// Expects a ServiceFound call to the observer `stream`.
    /// Panics if the call doesn't happen.
    fn expect_observer_service_found(
        exec: &mut fasync::Executor,
        stream: &mut PeerObserverRequestStream,
    ) {
        let observer_fut = stream.select_next_some();
        pin_mut!(observer_fut);

        match exec.run_until_stalled(&mut observer_fut) {
            Poll::Ready(Ok(PeerObserverRequest::ServiceFound { responder, .. })) => {
                let _ = responder.send();
            }
            x => panic!("Expected ServiceFound request but got: {:?}", x),
        }
    }

    /// Expects a ComponentTerminated call to the observer `stream`.
    /// Returns the URL of the terminated component.
    /// Panics if the call doesn't happen.
    fn expect_observer_component_terminated(
        exec: &mut fasync::Executor,
        stream: &mut PeerObserverRequestStream,
    ) -> String {
        let observer_fut = stream.select_next_some();
        pin_mut!(observer_fut);

        match exec.run_until_stalled(&mut observer_fut) {
            Poll::Ready(Ok(PeerObserverRequest::ComponentTerminated {
                component_url,
                responder,
                ..
            })) => {
                let _ = responder.send();
                return component_url;
            }
            x => panic!("Expected ComponentTerminated request but got: {:?}", x),
        }
    }

    /// Tests the behavior of launching profiles.
    /// Validates that the profiles can be launched successfully.
    /// Validates that when the launched profile terminates, the task that listens
    /// to component termination is correctly finished and removes the launched
    /// component from the MockPeer state.
    /// Validates that the observer relay is notified of component termination.
    #[test]
    fn test_launch_profiles() {
        let mut exec = fasync::Executor::new().unwrap();

        let id1 = PeerId(99);
        let (mut mock_peer, mut observer_stream, env_vars) =
            create_mock_peer(id1).expect("Mock peer creation should succeed");

        let profile_url1 = fuchsia_single_component_package_url!("bt-a2dp-source").to_string();
        let component_stream1 = do_launch_profile(&mut exec, profile_url1.clone(), &mut mock_peer);
        pin_mut!(component_stream1);

        // Launching the same profile is OK.
        let profile_url2 = fuchsia_single_component_package_url!("bt-a2dp-source").to_string();
        let component_stream2 = do_launch_profile(&mut exec, profile_url2.clone(), &mut mock_peer);
        pin_mut!(component_stream2);

        // Launching a different profile is OK.
        let profile_url3 = fuchsia_single_component_package_url!("bt-avrcp").to_string();
        let component_stream3 = do_launch_profile(&mut exec, profile_url3.clone(), &mut mock_peer);
        pin_mut!(component_stream3);

        // Dropping the client's searches and advertisements simulates component disconnection.
        {
            let mut w_env_vars = exec.run_singlethreaded(&mut env_vars.lock());
            w_env_vars.clear_advertisements();
            w_env_vars.clear_searches();
            let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());
        }

        // The component streams should resolve to the Terminated.
        match exec.run_singlethreaded(&mut component_stream1.next()) {
            Some(Ok(ComponentStatus::Terminated)) => {}
            x => panic!("Expected terminated but got: {:?}", x),
        }
        match exec.run_singlethreaded(&mut component_stream2.next()) {
            Some(Ok(ComponentStatus::Terminated)) => {}
            x => panic!("Expected terminated but got: {:?}", x),
        }
        match exec.run_singlethreaded(&mut component_stream3.next()) {
            Some(Ok(ComponentStatus::Terminated)) => {}
            x => panic!("Expected terminated but got: {:?}", x),
        }

        // We should receive 3 updates about component termination.
        let mut expected_urls = vec![profile_url1, profile_url2, profile_url3];
        let mut actual_urls = vec![];
        actual_urls.push(expect_observer_component_terminated(&mut exec, &mut observer_stream));
        actual_urls.push(expect_observer_component_terminated(&mut exec, &mut observer_stream));
        actual_urls.push(expect_observer_component_terminated(&mut exec, &mut observer_stream));
        assert_eq!(expected_urls.sort(), actual_urls.sort());
    }

    /// Tests registration of a new service followed by unregistration when the
    /// client drops the ConnectionReceiver.
    #[test]
    fn test_register_service() -> Result<(), Error> {
        let mut exec = fasync::Executor::new().unwrap();

        let id = PeerId(234);
        let (mut mock_peer, _observer_stream, _env_vars) = create_mock_peer(id)?;

        let (stream, adv_fut, svc_ids) = build_and_register_service(&mut mock_peer);
        pin_mut!(adv_fut);

        // Services should be advertised.
        let advertised_service_records = mock_peer.get_advertised_services(&svc_ids);
        assert_eq!(svc_ids, advertised_service_records.keys().cloned().collect());
        assert!(exec.run_until_stalled(&mut adv_fut).is_pending());

        // Client decides to not advertise its service anymore by dropping ServerEnd and
        // the listen-for-close future should resolve.
        drop(stream);
        assert!(exec.run_until_stalled(&mut adv_fut).is_ready());

        // Advertised services for the previously registered ServiceClassProfileIds should be gone.
        let advertised_service_records = mock_peer.get_advertised_services(&svc_ids);
        assert_eq!(HashSet::new(), advertised_service_records.keys().cloned().collect());

        Ok(())
    }

    /// Tests the registration of a new service and establishing a connection
    /// over potentially registered PSMs.
    #[test]
    fn test_register_service_with_connection() -> Result<(), Error> {
        let mut exec = fasync::Executor::new().unwrap();

        let id = PeerId(234);
        let (mut mock_peer, mut observer_stream, _env_vars) = create_mock_peer(id)?;

        // Build the A2DP Sink Service Definition.
        let (mut stream, _adv_fut, _svc_ids) = build_and_register_service(&mut mock_peer);

        // There should be no connection updates yet.
        match exec.run_until_stalled(&mut stream.next()) {
            Poll::Pending => {}
            x => panic!("Expected Pending but got: {:?}", x),
        }

        // An incoming connection request for PSM_AVCTP is invalid because PSM_AVCTP has not
        // been registered as a service.
        let remote_peer = PeerId(987);
        assert!(mock_peer.new_connection(remote_peer, Psm(PSM_AVCTP)).is_err());

        // An incoming connection request for PSM_AVDTP is valid, since it was registered
        // in `a2dp_def`. There should be a new connection request on the stream.
        assert!(mock_peer.new_connection(remote_peer, Psm(PSM_AVDTP)).is_ok());
        match exec.run_until_stalled(&mut stream.next()) {
            Poll::Ready(Some(Ok(ConnectionReceiverRequest::Connected {
                peer_id,
                channel,
                ..
            }))) => {
                assert_eq!(remote_peer, peer_id.into());
                assert_eq!(channel.channel_mode, Some(ChannelMode::Basic));
                assert_eq!(channel.max_tx_sdu_size, Some(DEFAULT_TX_SDU_SIZE as u16));
            }
            x => panic!("Expected Ready but got: {:?}", x),
        }
        // This should also be echo'ed on the observer.
        match exec.run_until_stalled(&mut observer_stream.next()) {
            Poll::Ready(Some(Ok(PeerObserverRequest::PeerConnected {
                peer_id,
                responder,
                ..
            }))) => {
                assert_eq!(remote_peer, peer_id.into());
                let _ = responder.send();
            }
            x => panic!("Expected Ready but got: {:?}", x),
        }

        Ok(())
    }

    /// Tests the registration of new searches. There can be multiple searches for the
    /// same ServiceClassProfileIdentifier.
    /// Tests notifying the outstanding searches with an advertised service.
    #[test]
    fn test_register_multiple_searches() -> Result<(), Error> {
        let mut exec = fasync::Executor::new().unwrap();

        let id = PeerId(234);
        let (mut mock_peer, mut observer_stream, _env_vars) = create_mock_peer(id)?;

        // The new search should be stored.
        let (mut stream1, search1) =
            build_and_register_search(&mut mock_peer, ServiceClassProfileIdentifier::AudioSink);
        pin_mut!(search1);
        let mut expected_searches = HashSet::new();
        expected_searches.insert(ServiceClassProfileIdentifier::AudioSink);
        assert_eq!(expected_searches, mock_peer.get_active_searches());

        // Adding a search for the same Service Class ID is OK.
        let (mut stream2, search2) =
            build_and_register_search(&mut mock_peer, ServiceClassProfileIdentifier::AudioSink);
        pin_mut!(search2);
        assert_eq!(expected_searches, mock_peer.get_active_searches());

        // Adding different search is OK.
        let (mut stream3, search3) = build_and_register_search(
            &mut mock_peer,
            ServiceClassProfileIdentifier::AvRemoteControl,
        );
        pin_mut!(search3);
        expected_searches.insert(ServiceClassProfileIdentifier::AvRemoteControl);
        assert_eq!(expected_searches, mock_peer.get_active_searches());

        // All three futures that listen for search termination should still be active.
        assert!(exec.run_until_stalled(&mut search1).is_pending());
        assert!(exec.run_until_stalled(&mut search2).is_pending());
        assert!(exec.run_until_stalled(&mut search3).is_pending());

        // Build a fake service as a registered record and notify any A2DP Sink searches.
        let mut record = build_a2dp_service_record(Psm(19));
        record.register_service_record(RegisteredServiceId::new(PeerId(999), 789)); // random
        let services = vec![record];
        mock_peer.notify_searches(&ServiceClassProfileIdentifier::AudioSink, services);

        // Only `stream1` and `stream2` correspond to searches for AudioSink.
        expect_search_service_found(&mut exec, &mut stream1);
        expect_search_service_found(&mut exec, &mut stream2);
        match exec.run_until_stalled(&mut stream3.next()) {
            Poll::Pending => {}
            x => panic!("Expected Pending but got: {:?}", x),
        }

        // Validate that the search results were observed by the PeerObserver for this MockPeer.
        // We expect the observer to be updated twice, since two different searches were notified.
        expect_observer_service_found(&mut exec, &mut observer_stream);
        expect_observer_service_found(&mut exec, &mut observer_stream);

        Ok(())
    }

    /// Tests that a service search is correctly termianted when the the client drops
    /// the ServerEnd of the SearchResults channel.
    #[test]
    fn test_search_termination() -> Result<(), Error> {
        let mut exec = fasync::Executor::new().unwrap();

        let id = PeerId(564);
        let (mut mock_peer, _observer_stream, _env_vars) = create_mock_peer(id)?;

        // The new search should be stored.
        let (stream1, search1) = build_and_register_search(
            &mut mock_peer,
            ServiceClassProfileIdentifier::AvRemoteControlTarget,
        );
        pin_mut!(search1);

        let mut expected_searches = HashSet::new();
        expected_searches.insert(ServiceClassProfileIdentifier::AvRemoteControlTarget);
        assert_eq!(expected_searches, mock_peer.get_active_searches());
        assert!(exec.run_until_stalled(&mut search1).is_pending());

        // Client decides it doesn't want to search for AudioSink anymore.
        drop(stream1);
        assert!(exec.run_until_stalled(&mut search1).is_ready());
        assert_eq!(HashSet::new(), mock_peer.get_active_searches());

        Ok(())
    }
}
