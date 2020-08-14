// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::PowerManagerError;
use crate::log_if_err;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::types::ThermalLoad;
use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_thermal as fthermal;
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceObjLocal};
use fuchsia_inspect::{self as inspect, Property};
use fuchsia_zircon::AsHandleRef;
use futures::prelude::*;
use futures::TryStreamExt;
use log::*;
use serde_json as json;
use std::cell::{Cell, RefCell};
use std::collections::HashMap;
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

pub struct ThermalLimiterBuilder<'a, 'b> {
    service_fs: Option<&'a mut ServiceFs<ServiceObjLocal<'b, ()>>>,
    inspect_root: Option<&'a inspect::Node>,
}

impl<'a, 'b> ThermalLimiterBuilder<'a, 'b> {
    pub fn new() -> Self {
        Self { service_fs: None, inspect_root: None }
    }

    pub fn new_from_json(
        _json_data: json::Value,
        _nodes: &HashMap<String, Rc<dyn Node>>,
        service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
    ) -> Self {
        Self::new().with_service_fs(service_fs)
    }

    pub fn with_service_fs(
        mut self,
        service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
    ) -> Self {
        self.service_fs = Some(service_fs);
        self
    }

    #[cfg(test)]
    pub fn with_inspect_root(mut self, root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(root);
        self
    }

    pub fn build(self) -> Result<Rc<ThermalLimiter>, Error> {
        // Optionally use the default inspect root node
        let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());

        let node = Rc::new(ThermalLimiter {
            clients: RefCell::new(Vec::new()),
            thermal_load: Cell::new(ThermalLoad(0)),
            inspect: InspectData::new(inspect_root, "ThermalLimiter".to_string()),
        });

        // Publish the Controller service only if we were provided with a ServiceFs
        if self.service_fs.is_some() {
            node.clone().publish_fidl_service(self.service_fs.unwrap());
        }

        Ok(node)
    }
}

/// Internal analogue of fthermal::TripPoint that uses ThermalLoad for its fields.
#[derive(Clone, Debug)]
struct TripPoint {
    deactivate_below: ThermalLoad,
    activate_at: ThermalLoad,
}

impl TripPoint {
    fn new(deactivate_below: u32, activate_at: u32) -> TripPoint {
        TripPoint {
            deactivate_below: ThermalLoad(deactivate_below),
            activate_at: ThermalLoad(activate_at),
        }
    }
}

impl From<fthermal::TripPoint> for TripPoint {
    fn from(tp: fthermal::TripPoint) -> TripPoint {
        TripPoint::new(tp.deactivate_below, tp.activate_at)
    }
}

impl Into<fthermal::TripPoint> for TripPoint {
    fn into(self: Self) -> fthermal::TripPoint {
        fthermal::TripPoint {
            deactivate_below: self.deactivate_below.0,
            activate_at: self.activate_at.0,
        }
    }
}

/// Contains state and connection information for one thermal client
struct ClientEntry {
    /// The Actor connection proxy to the client
    proxy: fthermal::ActorProxy,

    /// The type of subsystem that this client represents (unused for now)
    _actor_type: fthermal::ActorType,

    /// The current thermal state set on the client
    current_state: u32,

    /// The thermal load trip points that the client supplied when it subscribed
    trip_points: Vec<TripPoint>,

    /// The inspect node that this client records to. Since the ClientEntry owns the inspect node,
    /// if the ClientEntry is dropped then so will the node.
    _inspect_node: inspect::Node,

    /// An inspect property to represent the thermal state of this client
    inspect_state: inspect::UintProperty,
}

impl ClientEntry {
    fn new(
        proxy: fthermal::ActorProxy,
        actor_type: fthermal::ActorType,
        trip_points: Vec<TripPoint>,
        inspect_node: inspect::Node,
    ) -> Self {
        inspect_node.record_uint("proxy", proxy.as_handle_ref().raw_handle().into());
        inspect_node.record_uint("actor_type", actor_type as u64);
        for (i, trip_point) in trip_points.iter().enumerate() {
            let node = inspect_node.create_child(format!("trip_point_{:03}", i));
            node.record_uint("deactivate_below", trip_point.deactivate_below.0.into());
            node.record_uint("activate_at", trip_point.activate_at.0.into());
            inspect_node.record(node);
        }
        ClientEntry {
            proxy,
            _actor_type: actor_type,
            current_state: 0,
            trip_points,
            inspect_state: inspect_node.create_uint("thermal_state", 0),
            _inspect_node: inspect_node,
        }
    }

    /// Given a thermal load, determine and update the current thermal state locally. Returns the
    /// new state if the state was changed, otherwise returns None.
    fn update_state(&mut self, thermal_load: ThermalLoad) -> Option<u32> {
        let new_state =
            Self::determine_thermal_state(self.current_state, thermal_load, &self.trip_points);
        if new_state != self.current_state {
            self.current_state = new_state;
            self.inspect_state.set(new_state.into());
            Some(new_state)
        } else {
            None
        }
    }

    /// Given the current state, thermal load, and vector of trip points, determine the appropriate
    /// next thermal state.
    ///
    /// Since the current state is `current_state`, we know that the active trip points upon input
    /// are `trip_points[i]` for `i < current_state`.
    ///
    /// An active trip point becomes inactive if `thermal_load < trip_point.deactivate_below`,
    /// whereas an inactive trip point remains inactive if `thermal_load < trip_point.activate_at`.
    /// The output thermal state is the index of the first inactive trip point, or
    /// `len(trip_points)` if all trip points are active.
    fn determine_thermal_state(
        current_state: u32,
        thermal_load: ThermalLoad,
        trip_points: &Vec<TripPoint>,
    ) -> u32 {
        for (i, trip_point) in trip_points.iter().enumerate() {
            let threshold = if (i as u32) < current_state {
                trip_point.deactivate_below
            } else {
                trip_point.activate_at
            };
            if thermal_load < threshold {
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

    /// A struct for managing Component Inspection data
    inspect: InspectData,
}

impl ThermalLimiter {
    /// Start and publish the fuchsia.thermal.Controller service
    fn publish_fidl_service<'a, 'b>(
        self: Rc<Self>,
        service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
    ) {
        service_fs.dir("svc").add_fidl_service(move |stream: fthermal::ControllerRequestStream| {
            self.clone().handle_new_service_connection(stream);
        });
    }

    /// Called each time a client connects. For each client, a future is created to handle the
    /// request stream.
    fn handle_new_service_connection(
        self: Rc<Self>,
        mut stream: fthermal::ControllerRequestStream,
    ) {
        fuchsia_trace::instant!(
            "power_manager",
            "ThermalLimiter::handle_new_service_connection",
            fuchsia_trace::Scope::Thread
        );
        fasync::Task::local(
            async move {
                while let Some(req) = stream.try_next().await? {
                    match req {
                        // NOTE(fxb/57804): Copypasta between Subscribe and Subscribe2
                        // implementations is temporary until Subscribe2 replaces Subscribe.
                        fthermal::ControllerRequest::Subscribe {
                            actor,
                            actor_type,
                            trip_points,
                            responder,
                        } => {
                            fuchsia_trace::instant!(
                                "power_manager",
                                "ThermalLimiter::handle_subscribe",
                                fuchsia_trace::Scope::Thread
                            );
                            // A TripPoint with deactivate_below == activate_at is equivalent to the
                            // older style of trip point with a single thermal load specified.
                            let trip_points: Vec<fthermal::TripPoint> = trip_points
                                .into_iter()
                                .map(|val| fthermal::TripPoint {
                                    deactivate_below: val,
                                    activate_at: val,
                                })
                                .collect();
                            let mut result = self
                                .handle_new_client(actor.into_proxy()?, actor_type, trip_points)
                                .await;
                            log_if_err!(
                                result.map_err(|e| format!("{:?}", e)),
                                "Failed to handle new client"
                            );
                            fuchsia_trace::instant!(
                                "power_manager",
                                "ThermalLimiter::handle_new_client_result",
                                fuchsia_trace::Scope::Thread,
                                "result" => format!("{:?}", result).as_str()
                            );
                            let _ = responder.send(&mut result);
                        }
                        fthermal::ControllerRequest::Subscribe2 {
                            actor,
                            actor_type,
                            trip_points,
                            responder,
                        } => {
                            fuchsia_trace::instant!(
                                "power_manager",
                                "ThermalLimiter::handle_subscribe",
                                fuchsia_trace::Scope::Thread
                            );
                            let mut result = self
                                .handle_new_client(actor.into_proxy()?, actor_type, trip_points)
                                .await;
                            log_if_err!(
                                result.map_err(|e| format!("{:?}", e)),
                                "Failed to handle new client"
                            );
                            fuchsia_trace::instant!(
                                "power_manager",
                                "ThermalLimiter::handle_new_client_result",
                                fuchsia_trace::Scope::Thread,
                                "result" => format!("{:?}", result).as_str()
                            );
                            let _ = responder.send(&mut result);
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| error!("{:?}", e)),
        )
        .detach();
    }

    /// Handle a new client connection. Called each time a client makes the Subscribe call.
    async fn handle_new_client(
        &self,
        proxy: fthermal::ActorProxy,
        actor_type: fthermal::ActorType,
        trip_points: Vec<fthermal::TripPoint>,
    ) -> Result<(), fthermal::Error> {
        // TODO(fxb/44484): These strings must live for the duration of the function because the
        // trace macro uses them when the function goes out of scope. Therefore, they must be bound
        // here and not used anonymously at the macro callsite.
        let actor_type_str = format!("{:?}", actor_type);
        let trip_points_str = format!("{:?}", trip_points);
        fuchsia_trace::duration!(
            "power_manager",
            "ThermalLimiter::handle_new_client",
            "proxy" => proxy.as_handle_ref().raw_handle(),
            "actor_type" => actor_type_str.as_str(),
            "trip_points" => trip_points_str.as_str()
        );
        // trip_points must:
        //  - have length in the range [1 - MAX_TRIP_POINT_COUNT]
        //  - have `deactivate_below` <= `activate_at`
        //  - be monotonically increasing: `activate_at[i]` < `deactivate_below[i+1]`
        //  - have values in the range [1 - MAX_THERMAL_LOAD]
        let trip_points_len = trip_points.len() as u32;
        if trip_points_len < 1
            || trip_points_len > fthermal::MAX_TRIP_POINT_COUNT
            || !trip_points.iter().all(|p| p.deactivate_below <= p.activate_at)
            || !trip_points.windows(2).all(|w| w[0].activate_at < w[1].deactivate_below)
            || trip_points.first().unwrap().deactivate_below < 1
            || trip_points.last().unwrap().activate_at > MAX_THERMAL_LOAD.0
        {
            return Err(fthermal::Error::InvalidArguments);
        }

        // Convert trip_points from Vec<fthermal::TripPoint> to Vec<TripPoint>
        let trip_points: Vec<TripPoint> = trip_points.into_iter().map(|p| p.into()).collect();

        let mut client =
            ClientEntry::new(proxy, actor_type, trip_points, self.inspect.create_client_node());

        // Update the client state based on our last cached thermal load
        client.update_state(self.thermal_load.get());

        // Make a copy of the ClientEntry's proxy and state before ownership is moved into the
        // `clients` vector.
        let proxy = client.proxy.clone();
        let state = client.current_state;

        // Add the new client entry to the client list
        self.clients.borrow_mut().push(client);

        // Send the initial thermal state update to the client
        self.send_thermal_state(proxy, state);

        Ok(())
    }

    /// Handle an UpdateThermalLoad message. If the thermal load has not changed from the previous
    /// call, it is treated as a no-op.
    fn handle_update_thermal_load(
        &self,
        thermal_load: ThermalLoad,
    ) -> Result<MessageReturn, PowerManagerError> {
        fuchsia_trace::duration!(
            "power_manager",
            "ThermalLimiter::handle_update_thermal_load",
            "old_thermal_load" => self.thermal_load.get().0,
            "new_thermal_load" => thermal_load.0
        );
        if thermal_load > MAX_THERMAL_LOAD {
            return Err(PowerManagerError::InvalidArgument(format!(
                "Expected thermal_load in range [0-{}]; got {}",
                MAX_THERMAL_LOAD.0, thermal_load.0
            )));
        }

        // If the load hasn't changed, just bail here and return a success
        if thermal_load == self.thermal_load.get() {
            return Ok(MessageReturn::UpdateThermalLoad);
        }

        // Cache the thermal load value so if a new client connects we can give it the latest state
        self.thermal_load.set(thermal_load);
        self.inspect.thermal_load.set(thermal_load.0.into());

        // Check the client list for any closed proxies, and remove them from the list
        self.clients.borrow_mut().retain(|client| !client.proxy.is_closed());

        // Iterate through the clients to 1) update their local thermal state, and if that state
        // has changed, 2) create a future for the SetThermalState call out to that client.
        self.clients.borrow_mut().iter_mut().for_each(|client| {
            // If this client's state has changed...
            if let Some(state) = client.update_state(thermal_load) {
                self.send_thermal_state(client.proxy.clone(), state);
            }
        });

        Ok(MessageReturn::UpdateThermalLoad)
    }

    fn send_thermal_state(&self, proxy: fthermal::ActorProxy, state: u32) {
        // Spawn a future to update this client's thermal state
        fasync::Task::local(async move {
            fuchsia_trace::duration!(
                "power_manager",
                "ThermalLimiter::set_thermal_state",
                "state" => state,
                "proxy" => proxy.as_handle_ref().raw_handle()
            );
            let result = proxy.set_thermal_state(state).await;
            log_if_err!(result, "Failed to send thermal state to actor");
            fuchsia_trace::instant!(
                "power_manager",
                "ThermalLimiter::set_thermal_state_result",
                fuchsia_trace::Scope::Thread,
                "result" => format!("{:?}", result).as_str()
            );
        })
        .detach();
    }
}

#[async_trait(?Send)]
impl Node for ThermalLimiter {
    fn name(&self) -> String {
        "ThermalLimiter".to_string()
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, PowerManagerError> {
        match msg {
            Message::UpdateThermalLoad(thermal_load) => {
                self.handle_update_thermal_load(*thermal_load)
            }
            _ => Err(PowerManagerError::Unsupported),
        }
    }
}

struct InspectData {
    // Nodes
    clients_node: inspect::Node,

    // Properties
    thermal_load: inspect::UintProperty,

    // Internal
    unique_name_suffix: Cell<u32>,
}

impl InspectData {
    fn new(parent: &inspect::Node, name: String) -> Self {
        // Create a local root node and properties
        let root = parent.create_child(name);
        let thermal_load = root.create_uint("thermal_load", 0);
        let clients_node = root.create_child("clients");

        // Pass ownership of the new node to the parent node, otherwise it'll be dropped
        parent.record(root);

        InspectData { clients_node, thermal_load, unique_name_suffix: Cell::new(0) }
    }

    fn create_client_node(&self) -> inspect::Node {
        let unique_suffix = self.unique_name_suffix.get();
        self.unique_name_suffix.set(unique_suffix + 1);
        self.clients_node.create_child(format!("client{}", unique_suffix))
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use inspect::assert_inspect_tree;

    pub fn setup_test_node() -> Rc<ThermalLimiter> {
        ThermalLimiterBuilder::new().build().unwrap()
    }

    /// Creates an Actor proxy/stream and subscribes the proxy end to the given ThermalLimiter
    /// node. Returns the stream object where thermal state change events (SetThermalState calls)
    /// will be delivered.
    async fn subscribe_actor(
        node: Rc<ThermalLimiter>,
        actor_type: fthermal::ActorType,
        trip_points: Vec<TripPoint>,
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
        let mut trip_points: Vec<fthermal::TripPoint> =
            trip_points.into_iter().map(|p| p.into()).collect();
        controller_proxy
            .subscribe2(actor_proxy, actor_type, &mut trip_points.iter_mut())
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

    // Convenience function to construct a Vec<TripPoint> from an array or Vec of (u32, u32)
    fn make_trip_points<V>(values: V) -> Vec<TripPoint>
    where
        for<'a> &'a V: IntoIterator<Item = &'a (u32, u32)>,
    {
        values.into_iter().map(|v| TripPoint::new(v.0, v.1)).collect()
    }

    /// Tests that an unsupported message is handled gracefully and an Unsupported error is returned
    #[fasync::run_singlethreaded(test)]
    async fn test_unsupported_msg() {
        let node = setup_test_node();
        match node.handle_message(&Message::ReadTemperature).await {
            Err(PowerManagerError::Unsupported) => {}
            e => panic!("Unexpected return value: {:?}", e),
        }
    }

    /// Tests that an invalid thermal load update is met with an InvalidArgument error
    #[fasync::run_singlethreaded(test)]
    async fn test_invalid_thermal_load() {
        let node = setup_test_node();
        match node
            .handle_message(&Message::UpdateThermalLoad(ThermalLoad(MAX_THERMAL_LOAD.0 + 1)))
            .await
        {
            Err(PowerManagerError::InvalidArgument(_)) => {}
            e => panic!("Unexpected return value: {:?}", e),
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
        let trip_points = make_trip_points([(1, 1)]);
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
        let trip_points = make_trip_points([(1, 1)]);
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

        // More than fthermal::MAX_TRIP_POINT_COUNT trip points. Note that, depending on the value
        // of fthermal::MAX_THERMAL_LOAD, this may also violate the max allowed activate_at value of
        // a trip point. Regardless, this case will fail.
        let range = 1..=fthermal::MAX_TRIP_POINT_COUNT + 1;
        failure_trip_points.push(make_trip_points(range.clone().zip(range).collect::<Vec<_>>()));

        // deactivate_below > activate_at
        failure_trip_points.push(make_trip_points([(4, 3)]));

        // overlapping trip point boundaries
        failure_trip_points.push(make_trip_points([(7, 10), (9, 13)]));
        failure_trip_points.push(make_trip_points([(7, 10), (10, 13)]));

        // deactivate_below value of 0
        failure_trip_points.push(make_trip_points([(0, 1)]));

        // activate_at value greater than MAX_THERMAL_LOAD
        failure_trip_points.push(make_trip_points([(1, MAX_THERMAL_LOAD.0 + 1)]));

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
        let trip_points_client1 = make_trip_points([(1, 1), (50, 50), (100, 100)]);
        let trip_points_client2 = make_trip_points([(1, 1), (25, 25), (99, 99), (100, 100)]);

        // Set up the node and executor
        let mut exec = fasync::Executor::new().unwrap();
        let node = setup_test_node();

        // Set up the Actor clients
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
        let trip_points = make_trip_points([(50, 50)]);
        let expected_thermal_state = 0;

        let mut exec = fasync::Executor::new().unwrap();
        let node = setup_test_node();

        // Set up the actor stream with trip_points: [50]
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
        let determine_thermal_state = ClientEntry::determine_thermal_state;

        // One trip point, no hysteresis
        let trip_points = &make_trip_points([(7, 7)]);
        assert_eq!(determine_thermal_state(0, ThermalLoad(6), trip_points), 0);
        assert_eq!(determine_thermal_state(0, ThermalLoad(7), trip_points), 1);

        // One trip point with hysteresis
        let trip_points = &make_trip_points([(7, 8)]);
        assert_eq!(determine_thermal_state(0, ThermalLoad(6), trip_points), 0);
        assert_eq!(determine_thermal_state(0, ThermalLoad(7), trip_points), 0);
        assert_eq!(determine_thermal_state(0, ThermalLoad(8), trip_points), 1);
        assert_eq!(determine_thermal_state(1, ThermalLoad(8), trip_points), 1);
        assert_eq!(determine_thermal_state(1, ThermalLoad(7), trip_points), 1);
        assert_eq!(determine_thermal_state(1, ThermalLoad(6), trip_points), 0);

        // Two trip points with hysteresis -- check jumps of more than one state
        let trip_points = &make_trip_points([(7, 8), (13, 15)]);
        assert_eq!(determine_thermal_state(0, ThermalLoad(13), trip_points), 1);
        assert_eq!(determine_thermal_state(0, ThermalLoad(14), trip_points), 1);
        assert_eq!(determine_thermal_state(0, ThermalLoad(15), trip_points), 2);
        assert_eq!(determine_thermal_state(2, ThermalLoad(8), trip_points), 1);
        assert_eq!(determine_thermal_state(2, ThermalLoad(7), trip_points), 1);
        assert_eq!(determine_thermal_state(2, ThermalLoad(6), trip_points), 0);
    }

    /// Tests for the presence and correctness of dynamically-added inspect data
    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_data() {
        let inspector = inspect::Inspector::new();
        let node =
            ThermalLimiterBuilder::new().with_inspect_root(inspector.root()).build().unwrap();
        let actor_type = fthermal::ActorType::Unspecified;
        let trip_points = vec![TripPoint::new(47, 53)];

        // Subscribe a new client and block until we get the first state update from the Controller
        let mut stream = subscribe_actor(node.clone(), actor_type, trip_points).await.unwrap();
        assert_eq!(get_actor_state(&mut stream).await, 0);

        // Test that a client node is present for the new client
        assert_inspect_tree!(
            inspector,
            root: {
                ThermalLimiter: contains {
                    clients: {
                        client0: {
                            proxy: inspect::testing::AnyProperty,
                            actor_type: actor_type as u64,
                            trip_point_000: {
                                deactivate_below: 47u64,
                                activate_at: 53u64,
                            },
                            thermal_state: 0u64
                        }
                    }
                }
            }
        );
    }

    /// Tests that well-formed configuration JSON does not panic the `new_from_json` function.
    #[fasync::run_singlethreaded(test)]
    async fn test_new_from_json() {
        let json_data = json::json!({
            "type": "ThermalLimiter",
            "name": "thermal_limiter"
        });
        let _ = ThermalLimiterBuilder::new_from_json(
            json_data,
            &HashMap::new(),
            &mut ServiceFs::new_local(),
        );
    }
}
