// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::types::ThermalLoad;
use async_trait::async_trait;
use failure::{format_err, Error};
use fidl_fuchsia_thermal as fthermal;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::fx_log_err;
use futures::prelude::*;
use futures::TryStreamExt;
use std::cell::{Cell, RefCell};
use std::rc::Rc;

/// Node: ThermalLimiter
///
/// Summary: Hosts the fuchsia.thermal.Controller service which is responsbile for:
///     - providing the API for clients to subscribe as thermal actors to receive thermal state
///       change events
///     - calling out to the subscribed clients to notify them of their thermal state changes as
///       the system thermal load changes
///
/// Handles Messages:
///     - UpdateThermalLoad
///
/// Sends Messages: N/A
///
/// FIDL dependencies:
///     - fuchsia.thermal.Controller: the node hosts this service to provide a Subscribe API to any
///       clients interested in thermal limiting
///     - fuchsia.thermal.Actor: the node uses this protocol to call out to any of the
///       subscribed thermal clients

pub const MAX_THERMAL_LOAD: ThermalLoad = ThermalLoad(fthermal::MAX_THERMAL_LOAD);

/// Contains state and connection information for one thermal client
struct ClientEntry {
    /// The Actor connection proxy to the client
    proxy: fthermal::ActorProxy,

    /// The type of subsystem that this client represents (unused for now)
    _actor_type: fthermal::ActorType,

    /// The current thermal state set on the client
    current_state: u32,

    /// The thermal load trip points that the client supplied when it subscribed
    trip_points: Vec<ThermalLoad>,
}

impl ClientEntry {
    /// Given a thermal load, determine and update the current thermal state locally. Returns the
    /// new state if the state was changed, otherwise returns None.
    fn update_state(&mut self, thermal_load: ThermalLoad) -> Option<u32> {
        let new_state = Self::determine_thermal_state(thermal_load, &self.trip_points);
        if new_state != self.current_state {
            self.current_state = new_state;
            Some(new_state)
        } else {
            None
        }
    }

    /// Given a thermal load and vector of trip points, determine the appropriate thermal
    /// state. A thermal state is defined by the range between [previous_point - next_point), and
    /// is bounded on the ends by 0 and MAX_THERMAL_LOAD. Therefore, the determined state will be
    /// in the range [0 - len(trip_points)], a total of len(trip_points) + 1 possible states.
    fn determine_thermal_state(thermal_load: ThermalLoad, trip_points: &Vec<ThermalLoad>) -> u32 {
        for (i, trip_point) in trip_points.iter().enumerate() {
            if thermal_load < *trip_point {
                return i as u32;
            }
        }
        trip_points.len() as u32
    }
}

/// The ThermalLimiter node
pub struct ThermalLimiter {
    /// A list of all the connected clients. Disconnected clients are lazily purged from the list
    /// when the thermal load changes.
    clients: RefCell<Vec<ClientEntry>>,

    /// Cache of the last thermal load received
    thermal_load: Cell<ThermalLoad>,
}

impl ThermalLimiter {
    pub fn new() -> Result<Rc<Self>, Error> {
        let node = Self::create_node();
        node.clone().publish_fidl_service()?;
        Ok(node)
    }

    /// Creates a new ThermalLimiter node (but doesn't setup the Controller service)
    fn create_node() -> Rc<Self> {
        Rc::new(Self { clients: RefCell::new(Vec::new()), thermal_load: Cell::new(ThermalLoad(0)) })
    }

    /// Start and publish the fuchsia.thermal.Controller service
    fn publish_fidl_service(self: Rc<Self>) -> Result<(), Error> {
        let mut fs = ServiceFs::new_local();
        fs.dir("svc").add_fidl_service(move |stream: fthermal::ControllerRequestStream| {
            self.clone().handle_new_service_connection(stream);
        });
        fs.take_and_serve_directory_handle()?;
        Ok(())
    }

    /// Called each time a client connects. For each client, a future is created to handle the
    /// request stream.
    fn handle_new_service_connection(
        self: Rc<Self>,
        mut stream: fthermal::ControllerRequestStream,
    ) {
        fasync::spawn_local(
            async move {
                while let Some(req) = stream.try_next().await? {
                    match req {
                        fthermal::ControllerRequest::Subscribe {
                            actor,
                            actor_type,
                            trip_points,
                            responder,
                        } => {
                            let mut result = self
                                .handle_new_client(actor.into_proxy()?, actor_type, trip_points)
                                .await;
                            let _ = responder.send(&mut result);
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: failure::Error| fx_log_err!("{:?}", e)),
        );
    }

    /// Handle a new client connection. Called each time a client makes the Subscribe call.
    async fn handle_new_client(
        &self,
        proxy: fthermal::ActorProxy,
        actor_type: fthermal::ActorType,
        trip_points: Vec<u32>,
    ) -> Result<(), fthermal::Error> {
        // trip_points must:
        //  - have length in the range [1 - MAX_TRIP_POINT_COUNT]
        //  - have values in the range [1 - MAX_THERMAL_LOAD]
        //  - be monotonically increasing
        let trip_points_len = trip_points.len() as u32;
        if trip_points_len < 1
            || trip_points_len > fthermal::MAX_TRIP_POINT_COUNT
            || !trip_points.windows(2).all(|w| w[0] < w[1])
            || *trip_points.first().unwrap() < 1
            || *trip_points.last().unwrap() > MAX_THERMAL_LOAD.0
        {
            return Err(fthermal::Error::InvalidArguments);
        }

        // Convert trip_points from Vec<u32> to Vec<ThermalLoad>
        let trip_points = trip_points.into_iter().map(ThermalLoad).collect();

        let mut client =
            ClientEntry { proxy, _actor_type: actor_type, current_state: 0, trip_points };

        // Update the client state based on our last cached thermal load
        client.update_state(self.thermal_load.get());

        // Make a copy of the ClientEntry's proxy and state. We'll be using these in the spawned
        // future below, which has a lifetime exceeding that of this function.
        let proxy = client.proxy.clone();
        let state = client.current_state;

        // Add the new client entry to the client list
        self.clients.borrow_mut().push(client);

        // In a new task, call out to the client to provide the initial thermal state update
        fasync::spawn_local(async move {
            if let Err(e) = proxy.set_thermal_state(state).await {
                fx_log_err!("Failed to send thermal state to actor: {}", e);
            }
        });

        Ok(())
    }

    /// Handle an UpdateThermalLoad message. If the thermal load has not changed from the previous
    /// call, it is treated as a no-op.
    fn handle_update_thermal_load(
        &self,
        thermal_load: ThermalLoad,
    ) -> Result<MessageReturn, Error> {
        if thermal_load > MAX_THERMAL_LOAD {
            return Err(format_err!(
                "Expected thermal_load in range [0-{}]; got {}",
                MAX_THERMAL_LOAD.0,
                thermal_load.0
            ));
        }

        // If the load hasn't changed, just bail here and return a success
        if thermal_load == self.thermal_load.get() {
            return Ok(MessageReturn::UpdateThermalLoad);
        }

        // Cache the thermal load value so if a new client connects we can give it the latest state
        self.thermal_load.set(thermal_load);

        // Check the client list for any closed proxies, and remove them from the list
        self.clients.borrow_mut().retain(|client| !client.proxy.is_closed());

        // Iterate through the clients to 1) update their local thermal state, and if that state
        // has changed, 2) create a future for the SetThermalState call out to that client.
        self.clients.borrow_mut().iter_mut().for_each(|client| {
            // If this client's state has changed...
            if let Some(state) = client.update_state(thermal_load) {
                // Make a copy of the proxy because we'll need to use it in the Future (the
                // lifetime of the original proxy object will not last long enough)
                let proxy = client.proxy.clone();

                // Spawn a future to update this client's thermal state
                fasync::spawn_local(async move {
                    if let Err(e) = proxy.set_thermal_state(state).await {
                        fx_log_err!("Failed to send thermal state to actor: {}", e);
                    }
                });
            }
        });

        Ok(MessageReturn::UpdateThermalLoad)
    }
}

#[async_trait(?Send)]
impl Node for ThermalLimiter {
    fn name(&self) -> &'static str {
        "ThermalLimiter"
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, Error> {
        match msg {
            Message::UpdateThermalLoad(thermal_load) => {
                self.handle_update_thermal_load(*thermal_load)
            }
            _ => Err(format_err!("Unsupported message: {:?}", msg)),
        }
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;

    pub fn setup_test_node() -> Rc<ThermalLimiter> {
        ThermalLimiter::create_node()
    }

    /// Creates an Actor proxy/stream and subscribes the proxy end to the given ThermalLimiter
    /// node. Returns the stream object where thermal state change events (SetThermalState calls)
    /// will be delivered.
    async fn subscribe_actor(
        node: Rc<ThermalLimiter>,
        actor_type: fthermal::ActorType,
        trip_points: Vec<ThermalLoad>,
    ) -> Result<fthermal::ActorRequestStream, fthermal::Error> {
        // Create the proxy objects to be used for subscribing
        let (controller_proxy, controller_stream) =
            fidl::endpoints::create_proxy_and_stream::<fthermal::ControllerMarker>().unwrap();

        // Start the ThermalLimiter Controller server that will handle Subscribe calls from
        // controller_proxy
        node.handle_new_service_connection(controller_stream);

        // Create the proxy objects to be used for SetThermalState callbacks
        let (actor_proxy, actor_stream) =
            fidl::endpoints::create_request_stream::<fthermal::ActorMarker>().unwrap();

        // Subscribe with the ThermalLimiter Controller using the newly created actor proxy object
        // and supplied trip points
        controller_proxy
            .subscribe(actor_proxy, actor_type, &mut trip_points.iter().map(|tp| tp.0 as u32))
            .await
            .unwrap()?;

        // Return the actor stream object to receive state change events later
        Ok(actor_stream)
    }

    /// Returns the `state` contained within the first SetThermalState event in the
    /// actor_stream. Blocks until an event is received. If this function is being called with
    /// the expectation that there are no events, then the executor's `run_until_stalled` should be
    /// used to prevent a deadlock.
    async fn get_actor_state(actor_stream: &mut fthermal::ActorRequestStream) -> u32 {
        match actor_stream.try_next().await.unwrap() {
            Some(fthermal::ActorRequest::SetThermalState { state, responder }) => {
                let _ = responder.send();
                state
            }
            _ => panic!("Expected SetThermalState request"),
        }
    }

    /// Sends a message to the ThermalLimiter node to update its thermal load
    async fn set_thermal_load(node: &Rc<ThermalLimiter>, thermal_load: ThermalLoad) {
        match node.handle_message(&Message::UpdateThermalLoad(thermal_load)).await {
            Ok(MessageReturn::UpdateThermalLoad) => {}
            _ => panic!("Expected MessageReturn::UpdateThermalLoad"),
        }
    }

    /// Tests that when the actor first connects, the ThermalLimiter Controller sends an
    /// initial state update with the expected value
    #[fasync::run_until_stalled(test)]
    async fn test_initial_thermal_update() {
        let node = setup_test_node();

        // Test case 1
        // With this combination of thermal load and trip points, it is expected that the
        // ThermalLimiter Controller sends an update with thermal state 0 as soon as the Actor
        // connects
        let thermal_load = ThermalLoad(0);
        let trip_points = vec![ThermalLoad(1)];
        let expected_state = 0;

        set_thermal_load(&node, thermal_load).await;
        let mut stream =
            subscribe_actor(node.clone(), fthermal::ActorType::Unspecified, trip_points)
                .await
                .unwrap();
        assert_eq!(get_actor_state(&mut stream).await, expected_state);

        // Test case 2
        // With this combination of thermal load and trip points, it is expected that the
        // ThermalLimiter Controller sends an update with thermal state 1 as soon as the Actor
        // connects
        let thermal_load = ThermalLoad(50);
        let trip_points = vec![ThermalLoad(1)];
        let expected_state = 1;

        set_thermal_load(&node, thermal_load).await;
        let mut stream =
            subscribe_actor(node.clone(), fthermal::ActorType::Unspecified, trip_points)
                .await
                .unwrap();
        assert_eq!(get_actor_state(&mut stream).await, expected_state);
    }

    /// Tests that specifying invalid arguments to the Subscribe API results in the appropriate
    /// error
    #[fasync::run_until_stalled(test)]
    async fn test_invalid_subscribe_arguments() {
        let node = setup_test_node();

        // will contain sets of trip points that are expected to result in an InvalidArguments error
        let mut failure_trip_points = Vec::new();

        // empty trip points vector
        failure_trip_points.push(vec![]);

        // vector length greater than fthermal::MAX_TRIP_POINT_COUNT
        failure_trip_points
            .push((0..fthermal::MAX_TRIP_POINT_COUNT + 1).map(ThermalLoad).collect());

        // non-monotonically-increasing vector
        failure_trip_points.push(vec![ThermalLoad(2), ThermalLoad(1)]);
        failure_trip_points.push(vec![ThermalLoad(2), ThermalLoad(2)]);

        // a value of 0
        failure_trip_points.push(vec![ThermalLoad(0)]);

        // a value greater than MAX_THERMAL_LOAD
        failure_trip_points.push(vec![ThermalLoad(MAX_THERMAL_LOAD.0 + 1)]);

        for trip_points in failure_trip_points {
            match subscribe_actor(
                node.clone(),
                fthermal::ActorType::Unspecified,
                trip_points.to_vec(),
            )
            .await
            {
                Err(fthermal::Error::InvalidArguments) => {}
                _ => {
                    panic!("Expected Error::InvalidArguments for trip_points vec {:?}", trip_points)
                }
            };
        }
    }

    /// Tests that with the given thermal loads and trip points from multiple clients, the
    /// ThermalLimiter sends the expected thermal state change events to the appropriate clients
    #[test]
    fn test_thermal_state_updates() {
        // The test parameters are chosen such that the clients will have their thermal states
        // changed differently for the given thermal load changes. A state of "None" indicates that
        // there is no state change expected.
        #[derive(Debug)]
        struct TestCase {
            load: ThermalLoad,    // thermal load
            client1: Option<u32>, // expected state for client1
            client2: Option<u32>, // expected state for client2
        }
        let test_cases = vec![
            TestCase { load: ThermalLoad(0), client1: Some(0), client2: Some(0) },
            TestCase { load: ThermalLoad(20), client1: Some(1), client2: Some(1) },
            TestCase { load: ThermalLoad(40), client1: None, client2: Some(2) },
            TestCase { load: ThermalLoad(60), client1: Some(2), client2: None },
            TestCase { load: ThermalLoad(80), client1: None, client2: None },
            TestCase { load: ThermalLoad(100), client1: Some(3), client2: Some(4) },
        ];
        let trip_points_client1 = vec![1, 50, 100].into_iter().map(ThermalLoad).collect();
        let trip_points_client2 = vec![1, 25, 99, 100].into_iter().map(ThermalLoad).collect();

        // Setup the node and executor
        let mut exec = fasync::Executor::new().unwrap();
        let node = setup_test_node();

        // Setup the Actor clients
        let (mut stream_client1, mut stream_client2) = exec.run_singlethreaded(async {
            let stream1 = subscribe_actor(
                node.clone(),
                fthermal::ActorType::Unspecified,
                trip_points_client1,
            )
            .await
            .unwrap();

            let stream2 = subscribe_actor(
                node.clone(),
                fthermal::ActorType::Unspecified,
                trip_points_client2,
            )
            .await
            .unwrap();

            (stream1, stream2)
        });

        // Run through the test case list
        for test_case in test_cases {
            // Change the thermal load on the node
            exec.run_singlethreaded(set_thermal_load(&node, test_case.load));

            if let Some(expected_state) = test_case.client1 {
                // If client1 should have changed its state then verify the correct state
                assert_eq!(
                    exec.run_singlethreaded(get_actor_state(&mut stream_client1)),
                    expected_state,
                    "{:?}",
                    test_case
                );
            } else {
                // Otherwise check that it didn't receive any state changes
                assert!(
                    exec.run_until_stalled(&mut stream_client1.next()).is_pending(),
                    "{:?}",
                    test_case
                );
            }

            if let Some(expected_state) = test_case.client2 {
                // If client2 should have changed its state then verify the correct state
                assert_eq!(
                    exec.run_singlethreaded(get_actor_state(&mut stream_client2)),
                    expected_state,
                    "{:?}",
                    test_case
                );
            } else {
                // Otherwise check that it didn't receive any state changes
                assert!(
                    exec.run_until_stalled(&mut stream_client2.next()).is_pending(),
                    "{:?}",
                    test_case
                );
            }
        }
    }

    /// Tests that the ThermalLimiter only sends thermal state changes when state has actually
    /// changed (i.e., no duplicate state change events)
    #[test]
    fn test_no_duplicate_thermal_state_update() {
        // Test parameters chosen such that the new thermal load (25) doesn't cause a change in
        // expected thermal state
        let thermal_load = ThermalLoad(25);
        let trip_points = vec![ThermalLoad(50)];
        let expected_thermal_state = 0;

        let mut exec = fasync::Executor::new().unwrap();
        let node = setup_test_node();

        // Setup the actor stream with trip_points: [50]
        let mut stream = exec
            .run_singlethreaded(subscribe_actor(
                node.clone(),
                fthermal::ActorType::Unspecified,
                trip_points,
            ))
            .unwrap();

        // There should already be a "state 0" state change event waiting for us
        assert_eq!(exec.run_singlethreaded(get_actor_state(&mut stream)), expected_thermal_state);

        // Tell the ThermalLimiter that thermal load changed to 25
        exec.run_singlethreaded(set_thermal_load(&node, thermal_load));

        // Thermal load 25 is still within the fist thermal state, so there should be no state
        // change events waiting
        assert!(exec.run_until_stalled(&mut stream.next()).is_pending());
    }

    /// Tests that the `determine_thermal_state` function calculates the correct thermal state
    /// based on thermal load and trip point inputs
    #[test]
    fn test_client_determine_state() {
        let trip_points = &vec![ThermalLoad(1)];
        assert_eq!(ClientEntry::determine_thermal_state(ThermalLoad(0), trip_points), 0);
        assert_eq!(ClientEntry::determine_thermal_state(ThermalLoad(1), trip_points), 1);

        let trip_points = &vec![ThermalLoad(100)];
        assert_eq!(ClientEntry::determine_thermal_state(ThermalLoad(99), trip_points), 0);
        assert_eq!(ClientEntry::determine_thermal_state(ThermalLoad(100), trip_points), 1);

        let trip_points = &vec![ThermalLoad(50)];
        assert_eq!(ClientEntry::determine_thermal_state(ThermalLoad(49), trip_points), 0);
        assert_eq!(ClientEntry::determine_thermal_state(ThermalLoad(50), trip_points), 1);
        assert_eq!(ClientEntry::determine_thermal_state(ThermalLoad(51), trip_points), 1);

        let trip_points = &vec![ThermalLoad(1), ThermalLoad(50)];
        assert_eq!(ClientEntry::determine_thermal_state(ThermalLoad(0), trip_points), 0);
        assert_eq!(ClientEntry::determine_thermal_state(ThermalLoad(1), trip_points), 1);
        assert_eq!(ClientEntry::determine_thermal_state(ThermalLoad(49), trip_points), 1);
        assert_eq!(ClientEntry::determine_thermal_state(ThermalLoad(50), trip_points), 2);
        assert_eq!(ClientEntry::determine_thermal_state(ThermalLoad(51), trip_points), 2);
    }
}
