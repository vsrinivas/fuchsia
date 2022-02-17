// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::PowerManagerError;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::types::ThermalLoad;
use anyhow::{format_err, Error};
use async_trait::async_trait;
use async_utils::hanging_get::server as hanging_get;
use fidl_fuchsia_thermal as fthermal;
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceFsDir, ServiceObjLocal};
use fuchsia_inspect::{self as inspect, NumericProperty, Property};
use futures::prelude::*;
use futures::TryStreamExt;
use log::*;
use serde_json as json;
use std::cell::RefCell;
use std::collections::HashMap;
use std::iter::FromIterator as _;
use std::path::Path;
use std::rc::Rc;
use thermal_config::{ClientConfig, ThermalConfig};

/// Node: ThermalStateHandler
///
/// Summary: This node is responsible for hosting the `fuchsia.thermal.ClientStateConnector`
/// service, which allows thermal clients to connect to the service and retrieve their current
/// thermal state using a hanging-get pattern. A client's thermal state is determined according to
/// its central thermal configuration, which is detailed under
/// //src/power/power-manager/thermal_config/README.md. As thermal load changes are sent to this
/// node via the `UpdateThermalLoad` message, the resulting thermal state changes for each client
/// type are communicated to the respective clients using the `fuchsia.thermal.ClientStateWatcher`
/// protocol.
///
/// Handles Messages:
///   - UpdateThermalLoad
///
/// Sends Messages: N/A
///
/// FIDL dependencies:
///   - fuchsia.thermal.ClientStateConnector: the node hosts this service to allow thermal clients
///     to connect a `fuchsia.thermal.ClientStateWatcher` server end to the state of a specific
///     thermal client type.
///   - fuchsia.thermal.ClientStateWatcher: a client can provide the server end of a
///     `fuchsia.thermal.ClientStateWatcher` channel to be connected to the thermal state of a
///     specific thermal client type using the `fuchsia.thermal.ClientStateConnector/Connect`
///     method.

pub struct ThermalStateHandlerBuilder<'a, 'b> {
    thermal_config: Option<ThermalConfig>,
    outgoing_svc_dir: Option<ServiceFsDir<'a, ServiceObjLocal<'b, ()>>>,
    inspect_root: Option<&'a inspect::Node>,
}

impl<'a, 'b> ThermalStateHandlerBuilder<'a, 'b> {
    const THERMAL_CONFIG_PATH: &'static str = "/pkg/config/power_manager/thermal_config.json";

    pub fn new() -> Self {
        Self { thermal_config: None, outgoing_svc_dir: None, inspect_root: None }
    }

    pub fn new_from_json(
        _json_data: json::Value,
        _nodes: &HashMap<String, Rc<dyn Node>>,
        service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
    ) -> Self {
        Self::new().with_outgoing_svc_dir(service_fs.dir("svc"))
    }

    pub fn with_outgoing_svc_dir(
        mut self,
        outgoing_svc_dir: ServiceFsDir<'a, ServiceObjLocal<'b, ()>>,
    ) -> Self {
        self.outgoing_svc_dir = Some(outgoing_svc_dir);
        self
    }

    #[cfg(test)]
    pub fn with_inspect_root(mut self, root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(root);
        self
    }

    #[cfg(test)]
    fn with_thermal_config(mut self, thermal_config: ThermalConfig) -> Self {
        self.thermal_config = Some(thermal_config);
        self
    }

    pub fn build(self) -> Result<Rc<ThermalStateHandler>, Error> {
        // Create the root Inspect node for the ThermalStateHandler node. Allow inspect_root
        // override for tests.
        let inspect = self
            .inspect_root
            .unwrap_or(inspect::component::inspector().root())
            .create_child("ThermalStateHandler");

        // Read the thermal config file from `THERMAL_CONFIG_PATH`. Allow override for testing.
        let thermal_config = match self.thermal_config {
            Some(thermal_config) => thermal_config,
            None => ThermalConfig::read(&Path::new(Self::THERMAL_CONFIG_PATH))?,
        };

        let node = Rc::new(ThermalStateHandler {
            client_states: ClientStates::new(thermal_config, &inspect),
            _inspect: inspect,
        });

        // Publish the Controller service only if we were provided with a ServiceFs
        if let Some(outgoing_svc_dir) = self.outgoing_svc_dir {
            node.clone().publish_connector_service(outgoing_svc_dir);
        }

        Ok(node)
    }
}

/// The ThermalStateHandler node.
pub struct ThermalStateHandler {
    /// Configuration and state for all supported thermal clients.
    client_states: ClientStates,

    /// Root inspect node for the ThermalStateHandler.
    _inspect: inspect::Node,
}

/// Simple newtype to represent a client's thermal state value.
#[derive(Debug, PartialEq, PartialOrd, Copy, Clone)]
struct ThermalState(u32);

/// Stores the configuration and state for all supported thermal clients.
///
/// The underlying HashMap maps client type strings to their corresponding `ClientState` entry.
struct ClientStates(RefCell<HashMap<String, ClientState>>);

impl ClientStates {
    /// Creates a new `ClientStates` instance based on the provided `ThermalConfig`.
    ///
    /// The underlying map of the new `ClientStates` instance is created to contain a `ClientState`
    /// entry for each client type present in the provided `ThermalConfig`.
    fn new(thermal_config: ThermalConfig, inspect_parent: &inspect::Node) -> Self {
        let client_states =
            HashMap::from_iter(thermal_config.into_iter().map(|(client_type, client_config)| {
                let client_state = ClientState::new(
                    client_config,
                    new_state_broker(),
                    ClientStateInspect::new(inspect_parent.create_child(&client_type)),
                );
                (client_type, client_state)
            }));

        Self(RefCell::new(client_states))
    }

    /// Processes a new thermal load for the given sensor.
    ///
    /// This function takes the new thermal load value and simply passes it through to each
    /// contained `ClientState` entry.
    fn process_new_thermal_load(&self, thermal_load: ThermalLoad, sensor: &str) {
        fuchsia_trace::duration!(
            "power_manager",
            "ThermalStateHandler::process_new_thermal_load",
            "thermal_load" => thermal_load.0,
            "sensor" => sensor
        );

        self.0
            .borrow_mut()
            .values_mut()
            .for_each(|client_state| client_state.process_new_thermal_load(thermal_load, sensor));
    }

    /// Connects a `fuchsia.thermal.ClientStateWatcher` request stream to a specific client type.
    ///
    /// If successful, the incoming `Watch` requests on the stream will be completed with the
    /// thermal state of the given `client_type` as the state changes.
    fn connect_stream_for_client(
        &self,
        client_type: &str,
        stream: fthermal::ClientStateWatcherRequestStream,
    ) -> Result<(), Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "ThermalStateHandler::connect_stream_for_client",
            "client_type" => client_type
        );

        match self.0.borrow_mut().get_mut(client_type) {
            Some(client_state) => {
                client_state.connect_stream(stream);
                Ok(())
            }
            None => Err(format_err!("Unsupported client type: {}", client_type)),
        }
    }
}

/// Stores the configuration and state for a single thermal client.
struct ClientState {
    /// Vector of `TripPointState`s which forms the client's thermal config.
    trip_point_states: Vec<TripPointState>,

    /// We pass new thermal state values to the publisher, which takes care of updating the remote
    /// clients using hanging-gets.
    state_publisher: ClientStatePublisher,

    /// We use the broker to vend a new `ClientStateSubscriber` for each new `ClientStateWatcher`
    /// request stream.
    state_broker: ClientStateBroker,

    /// Cached `ThermalState` value. Simply used to determine if the value has changed.
    thermal_state: ThermalState,

    /// Structure to track and own Inspect data for the `ClientState`.
    inspect: ClientStateInspect,
}

impl ClientState {
    fn new(
        client_config: ClientConfig,
        state_broker: ClientStateBroker,
        inspect: ClientStateInspect,
    ) -> Self {
        // Create a vector of `TripPointState`s according to the provided `ClientConfig`
        let mut trip_point_states = create_trip_point_states_from_client_config(client_config);

        // Sort the vector in decreasing state order so when we iterate it in `get_thermal_state` we
        // select the highest thermal state
        trip_point_states.sort_by(|tps0, tps1| tps1.state.0.cmp(&tps0.state.0));

        Self {
            trip_point_states,
            state_publisher: state_broker.new_publisher(),
            state_broker,
            thermal_state: ThermalState(0),
            inspect,
        }
    }

    /// Connects a new `ClientStateWatcher` request stream to this client's thermal state.
    fn connect_stream(&mut self, stream: fthermal::ClientStateWatcherRequestStream) {
        self.inspect.connect_count.add(1);
        spawn_watcher_handler(stream, self.state_broker.new_subscriber());
    }

    /// Updates the client's active trip points based on the new thermal load for the given sensor.
    ///
    /// The function iterates through the vector of `TripPointState`s, filtering out those which do
    /// not match the sensor whose thermal load has changed, and sets each `TripPointState` to be
    /// active or inactive based on the new thermal load for the sensor.
    fn update_active_trip_points(&mut self, thermal_load: ThermalLoad, sensor: &str) {
        self.trip_point_states
            .iter_mut()
            .filter(|trip_point| trip_point.sensor == sensor)
            .for_each(|trip_point| {
                // For a trip point to be marked active, the thermal load must be at least greater
                // or equal to the `activate_at` threshold, OR the `deactivate_below` threshold if
                // the trip point is already active
                let activation_threshold = if trip_point.is_active {
                    trip_point.deactivate_below
                } else {
                    trip_point.activate_at
                };

                trip_point.is_active = thermal_load >= activation_threshold;
            });
    }

    /// Gets the current thermal state of the client.
    ///
    /// Iterates through the client's vector of `TripPointState`s, returning the `state` of the
    /// first encountered active trip point. Since `trip_point_states` is sorted in decreasing state
    /// order, this iteration will yield the highest thermal state of all activate trip points.
    fn get_thermal_state(&self) -> ThermalState {
        match self.trip_point_states.iter().find(|trip_point| trip_point.is_active) {
            Some(trip_point_state) => trip_point_state.state,
            None => ThermalState(0),
        }
    }

    /// Processes a new thermal load for the given sensor.
    ///
    /// First, each trip point of `trip_point_states` is updated to active/inactive based on the new
    /// thermal load for the given sensor. Next, the client's new thermal state is determined
    /// according to the new currently activate trip points. If the thermal state has changed, then
    /// the new value is passed to the publisher, where remote thermal clients will see the new
    /// value.
    fn process_new_thermal_load(&mut self, thermal_load: ThermalLoad, sensor: &str) {
        self.update_active_trip_points(thermal_load, sensor);

        let new_thermal_state = self.get_thermal_state();
        if new_thermal_state != self.thermal_state {
            self.thermal_state = new_thermal_state;
            self.inspect.thermal_state.set(new_thermal_state.0.into());
            self.state_publisher.set(new_thermal_state);
        }
    }
}

/// A structure that correlates a trip point (and underlying `sensor, `activate_at`, and
/// `deactivate_below` configuration) to a resulting thermal state and active status.
struct TripPointState {
    sensor: String,
    activate_at: ThermalLoad,
    deactivate_below: ThermalLoad,
    is_active: bool,
    state: ThermalState,
}

/// Creates a vector of `TripPointState`s from the given `ClientConfig`.
///
/// The vector is essentially just a flattened out view of the `ClientConfig`, which allows for
/// convenient iteration later when determining which trip points and states are active.
fn create_trip_point_states_from_client_config(client_config: ClientConfig) -> Vec<TripPointState> {
    client_config
        .into_thermal_states()
        .into_iter()
        .map(|state_config| {
            let state = ThermalState(state_config.state);
            state_config.trip_points.into_iter().map(move |trip_point| TripPointState {
                sensor: trip_point.sensor,
                activate_at: ThermalLoad(trip_point.activate_at),
                deactivate_below: ThermalLoad(trip_point.deactivate_below),
                is_active: false,
                state,
            })
        })
        .flatten()
        .collect()
}

/// Spawns a `Task` to handle `Watch` requests from a `fuchsia.thermal.ClientStateWatcher` channel.
///
/// The `Watch` requests will be fulfilled by registering them with the provided `subscriber`. The
/// `subscriber` is tied to the thermal state for a specific client. Therefore, the `Watch` requests
/// will be responded to with the thermal state of the specific client type of `subscriber`.
fn spawn_watcher_handler(
    mut stream: fthermal::ClientStateWatcherRequestStream,
    subscriber: ClientStateSubscriber,
) {
    fuchsia_trace::duration!("power_manager", "ThermalStateHandler::spawn_watcher_handler");

    fasync::Task::local(
        async move {
            while let Some(fthermal::ClientStateWatcherRequest::Watch { responder }) =
                stream.try_next().await?
            {
                fuchsia_trace::duration!(
                    "power_manager",
                    "ThermalStateHandler::spawn_watcher_handler::Watch"
                );

                // The responder for the `Watch` FIDL request is now owned by the subscriber. The
                // request will be completed with the new thermal state once it is ready.
                subscriber.register(responder)?
            }

            Ok(())
        }
        .unwrap_or_else(|e: anyhow::Error| error!("{:?}", e)),
    )
    .detach();
}

/// A structure to own and track Inspect data for a single `ClientState` instance.
struct ClientStateInspect {
    thermal_state: inspect::UintProperty,

    // TODO(fxbug.dev/93970): track # of active connections instead of just connect count
    connect_count: inspect::UintProperty,
    _client_node: inspect::Node,
}

impl ClientStateInspect {
    fn new(client_node: inspect::Node) -> Self {
        let thermal_state = client_node.create_uint("thermal_state", 0);
        let connect_count = client_node.create_uint("connect_count", 0);
        Self { thermal_state, connect_count, _client_node: client_node }
    }
}

// Below are a series of type aliases for convenience
type WatchResponder = fthermal::ClientStateWatcherWatchResponder;
type StateChangeFn = Box<dyn Fn(&ThermalState, WatchResponder) -> bool>;
type ClientStateBroker = hanging_get::HangingGet<ThermalState, WatchResponder, StateChangeFn>;
type ClientStatePublisher = hanging_get::Publisher<ThermalState, WatchResponder, StateChangeFn>;
type ClientStateSubscriber = hanging_get::Subscriber<ThermalState, WatchResponder, StateChangeFn>;

/// Convenience function to create a new `ClientStateBroker` instance.
///
/// The broker is used to vend new `ClientStatePublisher` and `ClientStateSubscriber` instances.
///
/// When `ClientStatePublisher.set()` is called with a `ThermalState` value, then all pending
/// `Watch` requests registered with a corresponding `ClientStateSubscriber` (see the
/// `spawn_watcher_handler` function) will be completed with that value, regardless of whether that
/// value differs from the previous `ThermalState` that was sent to the client. Therefore, care
/// should be taken to only call `ClientStatePublisher.set()` when the `ThermalState` value has
/// actually changed in order to properly implement the hanging-get behavior.
fn new_state_broker() -> ClientStateBroker {
    let notify_fn: StateChangeFn = Box::new(|state, responder| {
        match responder.send(state.0 as u64) {
            Ok(()) => true, // indicates that the client was successfully updated
            Err(e) => {
                error!("Failed to send thermal state to client: {}", e);
                false
            }
        }
    });
    hanging_get::HangingGet::new(ThermalState(0), notify_fn)
}

impl ThermalStateHandler {
    /// Publishes the `fuchsia.thermal.ClientStateConnector` service.
    ///
    /// For each new connection, `spawn_connector_handler` is called with that connection's request
    /// stream.
    fn publish_connector_service<'a, 'b>(
        self: Rc<Self>,
        mut outgoing_svc_dir: ServiceFsDir<'a, ServiceObjLocal<'b, ()>>,
    ) {
        outgoing_svc_dir.add_fidl_service(
            move |stream: fthermal::ClientStateConnectorRequestStream| {
                self.clone().spawn_connector_handler(stream);
            },
        );
    }

    /// Spawns a `Task` to handle `Connect` requests from a `fuchsia.thermal.ClientStateConnector`
    /// channel.
    ///
    /// The `Connect` requests contain a `client_type` value and a
    /// `fuchsia.thermal.ClientStateWatcher` server end. If the request is valid, then the `Watch`
    /// requests on the provided `ClientStateWatcher` server end will be responded to with the
    /// thermal state of the given `client_type`.
    fn spawn_connector_handler(
        self: Rc<Self>,
        mut stream: fthermal::ClientStateConnectorRequestStream,
    ) {
        fuchsia_trace::duration!("power_manager", "ThermalStateHandler::spawn_connector_handler");

        fasync::Task::local(
            async move {
                while let Some(req) = stream.try_next().await? {
                    match req {
                        fthermal::ClientStateConnectorRequest::Connect {
                            client_type,
                            watcher,
                            ..
                        } => self
                            .client_states
                            .connect_stream_for_client(&client_type, watcher.into_stream()?)?,
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| error!("{:?}", e)),
        )
        .detach();
    }

    /// Handles an `UpdateThermalLoad` message.
    ///
    /// The new thermal load is checked for validity then passed on to each `ClientState` entry for
    /// further processing.
    fn handle_update_thermal_load(
        &self,
        thermal_load: ThermalLoad,
        sensor: &str,
    ) -> Result<MessageReturn, PowerManagerError> {
        fuchsia_trace::duration!(
            "power_manager",
            "ThermalStateHandler::handle_update_thermal_load",
            "thermal_load" => thermal_load.0,
            "sensor" => sensor
        );

        if thermal_load > ThermalLoad(fthermal::MAX_THERMAL_LOAD) {
            return Err(PowerManagerError::InvalidArgument(format!(
                "Thermal load {:?} exceeds max {}",
                thermal_load,
                fthermal::MAX_THERMAL_LOAD
            )));
        }

        self.client_states.process_new_thermal_load(thermal_load, sensor);

        Ok(MessageReturn::UpdateThermalLoad)
    }
}

#[async_trait(?Send)]
impl Node for ThermalStateHandler {
    fn name(&self) -> String {
        "ThermalStateHandler".to_string()
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, PowerManagerError> {
        match msg {
            Message::UpdateThermalLoad(thermal_load, sensor) => {
                self.handle_update_thermal_load(*thermal_load, sensor)
            }
            _ => Err(PowerManagerError::Unsupported),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use fuchsia_inspect::assert_data_tree;
    use std::task::Poll;
    use thermal_config::TripPoint;

    // Takes a ServiceFs which contains the ThermalStateHandler node's implementation of the
    // `fuchsia.thermal.ClientStateConnector` protocol and manages the underlying test
    // infrastructure required to use the service.
    struct TestEnv {
        env: fuchsia_component::server::NestedEnvironment,
    }

    impl TestEnv {
        // Takes a ServiceFs and creates a nested environment which we'll later use for connecting
        // to the ThermalStateHandler node's implementation of the
        // `fuchsia.thermal.ClientStateConnector` protocol.
        fn new(mut service_fs: ServiceFs<ServiceObjLocal<'static, ()>>) -> Self {
            let env = service_fs.create_nested_environment("env").unwrap();
            fasync::Task::local(service_fs.collect()).detach();
            Self { env }
        }

        // Connects to the `ClientStateConnector` protocol contained within the `NestedEnvironment`
        // and uses this protocol to connect a fake client of the given type.
        fn connect_client(&self, client_type: &str) -> FakeClient {
            let connector =
                self.env.connect_to_protocol::<fthermal::ClientStateConnectorMarker>().unwrap();

            let (watcher_proxy, watcher_server_end) =
                fidl::endpoints::create_proxy::<fthermal::ClientStateWatcherMarker>().unwrap();

            // Pass the `watcher_server_end` to the node, so it will be associated with thermal
            // state changes of `client_type`
            assert_matches!(connector.connect(client_type, watcher_server_end), Ok(()));

            FakeClient { watcher_proxy, hanging_watcher_request: RefCell::new(None) }
        }
    }

    // A fake thermal client capable of connecting to the `ThermalStateHandler` node to watch for
    // thermal state changes.
    struct FakeClient {
        watcher_proxy: fthermal::ClientStateWatcherProxy,
        hanging_watcher_request: RefCell<Option<fidl::client::QueryResponseFut<u64>>>,
    }

    impl FakeClient {
        // Gets the thermal state for the fake client.
        //
        // Since requests are using hanging-get, there are three possible return values to consider:
        //  - Ok(None) = the watch request succeeded but there are no updates to the client's
        //     thermal state (the request is now "hanging")
        //  - Ok(Some(state)) = the watch request succeeded and returned a new thermal state
        //  - Err(e) = the watch request failed
        fn get_thermal_state(
            &self,
            executor: &mut fasync::TestExecutor,
        ) -> Result<Option<ThermalState>, Error> {
            // If there's already a hanging request (the previous call to `get_thermal_state`
            // returned `Ok(None)`), then check if that request has a response for us. If there
            // wasn't already a hanging request, then send a new one on the channel
            let mut watch_request = self
                .hanging_watcher_request
                .take() // take the Option from the RefCell
                .take() // take the pending request (if any) from the Option
                .unwrap_or_else(|| self.watcher_proxy.watch());

            match executor.run_until_stalled(&mut watch_request) {
                Poll::Pending => {
                    // The request is now "hanging" with the server. Cache it so we can check it for
                    // a response in subsequent calls to `get_thermal_state`.
                    self.hanging_watcher_request.replace(Some(watch_request));
                    Ok(None)
                }
                Poll::Ready(Ok(state)) => Ok(Some(ThermalState(state as u32))),
                Poll::Ready(Err(e)) => Err(e.into()),
            }
        }
    }

    /// Tests that well-formed configuration JSON does not panic the `new_from_json` function.
    #[fasync::run_singlethreaded(test)]
    async fn test_new_from_json() {
        let json_data = json::json!({
            "type": "ThermalStateHandler",
            "name": "thermal_state_handler"
        });
        let _ = ThermalStateHandlerBuilder::new_from_json(
            json_data,
            &HashMap::new(),
            &mut ServiceFs::new_local(),
        );
    }

    /// Tests that each thermal client's state is correctly published into Inspect.
    #[test]
    fn test_inspect() {
        let mut executor = fasync::TestExecutor::new().unwrap();
        let mut service_fs = ServiceFs::new_local();

        // Create a test config with two clients to verify each has their respective Inspect nodes
        // updated independently
        let thermal_config = ThermalConfig::new()
            .add_client_config(
                "client1",
                ClientConfig::new().add_thermal_state(vec![TripPoint::new("sensor1", 5, 10)]),
            )
            .add_client_config(
                "client2",
                ClientConfig::new().add_thermal_state(vec![TripPoint::new("sensor1", 15, 20)]),
            );

        let inspector = inspect::Inspector::new();
        let node = ThermalStateHandlerBuilder::new()
            .with_inspect_root(inspector.root())
            .with_thermal_config(thermal_config)
            .with_outgoing_svc_dir(service_fs.root_dir())
            .build()
            .unwrap();

        let test_env = TestEnv::new(service_fs);

        // Check for default initialized values
        assert_data_tree!(
            inspector,
            root: {
                ThermalStateHandler: {
                    client1: {
                        thermal_state: 0u64,
                        connect_count: 0u64
                    },
                    client2: {
                        thermal_state: 0u64,
                        connect_count: 0u64
                    }
                }
            }
        );

        // Connect client2 and verify its `connect_count` is incremented
        let client2 = test_env.connect_client("client2");
        assert_matches!(client2.get_thermal_state(&mut executor), Ok(Some(ThermalState(0))));

        assert_data_tree!(
            inspector,
            root: {
                ThermalStateHandler: {
                    client1: {
                        thermal_state: 0u64,
                        connect_count: 0u64
                    },
                    client2: {
                        thermal_state: 0u64,
                        connect_count: 1u64
                    }
                }
            }
        );

        // Update the thermal state for client1 and verify its `thermal_state` is updated
        node.handle_update_thermal_load(ThermalLoad(10), "sensor1").unwrap();

        assert_data_tree!(
            inspector,
            root: {
                ThermalStateHandler: {
                    client1: {
                        thermal_state: 1u64,
                        connect_count: 0u64
                    },
                    client2: {
                        thermal_state: 0u64,
                        connect_count: 1u64
                    }
                }
            }
        );
    }

    /// Tests that the server correctly implements the hanging-get pattern.
    #[test]
    fn test_hanging_get() {
        let mut executor = fasync::TestExecutor::new().unwrap();
        let mut service_fs = ServiceFs::new_local();

        let thermal_config = ThermalConfig::new().add_client_config(
            "client1",
            ClientConfig::new().add_thermal_state(vec![TripPoint::new("sensor1", 0, 10)]),
        );

        let node = ThermalStateHandlerBuilder::new()
            .with_thermal_config(thermal_config)
            .with_outgoing_svc_dir(service_fs.root_dir())
            .build()
            .unwrap();

        let test_env = TestEnv::new(service_fs);
        let client = test_env.connect_client("client1");

        // First request gives initial thermal state
        assert_matches!(client.get_thermal_state(&mut executor), Ok(Some(ThermalState(0))));

        // Second request has no update
        assert_matches!(client.get_thermal_state(&mut executor), Ok(None));

        // Now update the thermal load
        node.handle_update_thermal_load(ThermalLoad(10), "sensor1").unwrap();

        // Verify the client now gets the thermal state change response
        assert_matches!(client.get_thermal_state(&mut executor), Ok(Some(ThermalState(1))));

        // Update thermal load, but the client's state is unchanged
        node.handle_update_thermal_load(ThermalLoad(20), "sensor1").unwrap();

        // Verify there is no new response for the client
        assert_matches!(client.get_thermal_state(&mut executor), Ok(None));
    }

    /// Tests that a connect request from an unsupported `client_type` returns an error.
    #[test]
    fn test_unsupported_client() {
        let mut executor = fasync::TestExecutor::new().unwrap();
        let mut service_fs = ServiceFs::new_local();

        let _node = ThermalStateHandlerBuilder::new()
            .with_thermal_config(ThermalConfig::new())
            .with_outgoing_svc_dir(service_fs.root_dir())
            .build()
            .unwrap();

        let test_env = TestEnv::new(service_fs);
        let client = test_env.connect_client("client1");

        // Connect a client for the "client1" client type, which is not specified in our
        // ThermalConfig
        assert_matches!(client.get_thermal_state(&mut executor), Err(_));
    }

    /// Tests that an invalid thermal load update is met with an InvalidArgument error
    #[fasync::run_singlethreaded(test)]
    async fn test_invalid_thermal_load() {
        let node = ThermalStateHandlerBuilder::new()
            .with_thermal_config(ThermalConfig::new())
            .build()
            .unwrap();

        let result = node
            .handle_message(&Message::UpdateThermalLoad(
                ThermalLoad(fthermal::MAX_THERMAL_LOAD + 1),
                String::new(),
            ))
            .await;

        assert_matches!(result, Err(PowerManagerError::InvalidArgument(_)));
    }

    /// Tests that we deliver the correct thermal state to a client, even if its state has changed
    /// before the client has connected.
    #[test]
    fn test_initial_thermal_state() {
        let mut executor = fasync::TestExecutor::new().unwrap();
        let mut service_fs = ServiceFs::new_local();

        let thermal_config = ThermalConfig::new().add_client_config(
            "client1",
            ClientConfig::new().add_thermal_state(vec![TripPoint::new("sensor1", 0, 10)]),
        );

        let node = ThermalStateHandlerBuilder::new()
            .with_thermal_config(thermal_config)
            .with_outgoing_svc_dir(service_fs.root_dir())
            .build()
            .unwrap();

        let test_env = TestEnv::new(service_fs);
        let client = test_env.connect_client("client1");

        // Set the initial thermal load before the client connects
        node.handle_update_thermal_load(ThermalLoad(10), "sensor1").unwrap();

        // When the client first connects, verify they get the latest thermal state
        assert_matches!(client.get_thermal_state(&mut executor), Ok(Some(ThermalState(1))));
    }

    /// Tests that multiple clients connected simultaneously receive their appropriate thermal state
    /// updates.
    #[test]
    fn test_multiple_client_types() {
        let mut executor = fasync::TestExecutor::new().unwrap();
        let mut service_fs = ServiceFs::new_local();

        let thermal_config = ThermalConfig::new()
            .add_client_config(
                "client1",
                ClientConfig::new()
                    .add_thermal_state(vec![TripPoint::new("sensor1", 1, 9)])
                    .add_thermal_state(vec![TripPoint::new("sensor1", 10, 19)]),
            )
            .add_client_config(
                "client2",
                ClientConfig::new()
                    .add_thermal_state(vec![TripPoint::new("sensor1", 10, 19)])
                    .add_thermal_state(vec![TripPoint::new("sensor1", 20, 29)]),
            );

        let node = ThermalStateHandlerBuilder::new()
            .with_thermal_config(thermal_config)
            .with_outgoing_svc_dir(service_fs.root_dir())
            .build()
            .unwrap();

        let test_env = TestEnv::new(service_fs);
        let client1 = test_env.connect_client("client1");
        let client2 = test_env.connect_client("client2");

        // First request gives initial thermal state for both clients
        assert_matches!(client1.get_thermal_state(&mut executor), Ok(Some(ThermalState(0))));
        assert_matches!(client2.get_thermal_state(&mut executor), Ok(Some(ThermalState(0))));

        // Update the thermal load for "sensor1" and verify each client is in their expected state
        node.handle_update_thermal_load(ThermalLoad(19), "sensor1").unwrap();
        assert_matches!(client1.get_thermal_state(&mut executor), Ok(Some(ThermalState(2))));
        assert_matches!(client2.get_thermal_state(&mut executor), Ok(Some(ThermalState(1))));

        // Update thermal load for "sensor1" once more
        node.handle_update_thermal_load(ThermalLoad(29), "sensor1").unwrap();

        // client1 state is unchanged, but client2 moves to state 2
        assert_matches!(client1.get_thermal_state(&mut executor), Ok(None));
        assert_matches!(client2.get_thermal_state(&mut executor), Ok(Some(ThermalState(2))));
    }
}
