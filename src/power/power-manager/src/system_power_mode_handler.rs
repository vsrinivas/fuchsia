// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::log_if_err;
use crate::node::Node;
use anyhow::{format_err, Error};
use async_utils::hanging_get::server as hanging_get;
use fidl_fuchsia_power_clientlevel as fpowerclient;
use fidl_fuchsia_power_systemmode as fpowermode;
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceFsDir, ServiceObjLocal};
use fuchsia_inspect::{self as inspect, NumericProperty, Property};
use futures::prelude::*;
use futures::TryStreamExt;
use log::*;
use serde_json as json;
use std::cell::RefCell;
use std::collections::{HashMap, HashSet};
use std::path::Path;
use std::rc::Rc;
use system_power_mode_config::{
    ClientConfig, ClientConfigExt, ClientType, SystemMode, SystemPowerModeConfig,
};

/// Node: SystemPowerModeHandler
///
/// Summary: This node hosts a few services related to system power modes and client power levels.
/// The services allow clients to connect using `fuchsia.power.clientlevel.Connector` to retrieve
/// their current power level using a hanging-get pattern. A client's power level is determined
/// according to its system power mode configuration, which is detailed under
/// //src/power/power-manager/system_power_mode_config/README.md. As the set of active system power
/// modes changes via the `fuchsia.power.systemmode.Requester` service (which this node also hosts),
/// the power level for each configured client will be reevaluated and communicated using the
/// `fuchsia.power.clientlevel.Watcher` protocol.
///
/// Handles Messages: N/A
///
/// Sends Messages: N/A
///
/// FIDL dependencies:
///   - fuchsia.power.clientlevel.Connector: the node hosts this service to allow clients to connect
///     a `fuchsia.power.clientlevel.Watcher` server end to the power level of a specific client
///     type.
///   - fuchsia.power.clientlevel.Watcher: a client can provide the server end of a
///     `fuchsia.power.clientlevel.Watcher` channel to be connected to the power level of a specific
///     client type using the `fuchsia.power.clientlevel/Connector.Connect` method.

pub struct SystemPowerModeHandlerBuilder<'a, 'b> {
    system_power_mode_config: Option<SystemPowerModeConfig>,
    outgoing_svc_dir: Option<ServiceFsDir<'a, ServiceObjLocal<'b, ()>>>,
    inspect_root: Option<&'a inspect::Node>,
}

impl<'a, 'b> SystemPowerModeHandlerBuilder<'a, 'b> {
    const SYSTEM_POWER_MODE_CONFIG_PATH: &'static str =
        "/pkg/config/power_manager/system_power_mode_config.json";

    pub fn new() -> Self {
        Self { system_power_mode_config: None, outgoing_svc_dir: None, inspect_root: None }
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
    fn with_system_power_mode_config(mut self, config: SystemPowerModeConfig) -> Self {
        self.system_power_mode_config = Some(config);
        self
    }

    pub fn build(self) -> Result<Rc<SystemPowerModeHandler>, Error> {
        // Create the root Inspect node for the `SystemPowerModeHandler` node. Allow inspect_root
        // override for tests.
        let inspect_root = self
            .inspect_root
            .unwrap_or(inspect::component::inspector().root())
            .create_child("SystemPowerModeHandler");

        // Read the system power mode config file from `SYSTEM_POWER_MODE_CONFIG_PATH`. Allow
        // override for testing.
        let system_power_mode_config = match self.system_power_mode_config {
            Some(system_power_mode_config) => system_power_mode_config,
            None => SystemPowerModeConfig::read(&Path::new(Self::SYSTEM_POWER_MODE_CONFIG_PATH))?,
        };

        // Create the `ClientStates` to manage states for all clients
        let client_states =
            ClientStates::new(system_power_mode_config, inspect_root.create_child("clients"));

        // Initialize default power levels for all clients
        client_states.process_system_power_modes_changed(&HashSet::new());

        let node = Rc::new(SystemPowerModeHandler {
            client_states,
            system_power_modes: RefCell::new(HashSet::new()),
            inspect: SystemPowerModeHandlerInspect::new(inspect_root),
        });

        node.clone().publish_services(
            self.outgoing_svc_dir.ok_or(format_err!("Missing outgoing_svc_dir"))?,
        );

        Ok(node)
    }
}

/// The SystemPowerModeHandler node.
pub struct SystemPowerModeHandler {
    /// Internal configuration and state for all configured clients.
    client_states: ClientStates,

    /// Contains the set of currently active system power modes.
    system_power_modes: RefCell<HashSet<SystemMode>>,

    /// Holds inspect properties for the top-level "SystemPowerModeHandler" inspect node.
    inspect: SystemPowerModeHandlerInspect,
}

/// Stores the internal configuration and state for all configured clients.
struct ClientStates {
    /// Maps a `ClientType` to the corresponding `ClientState`.
    states: RefCell<HashMap<ClientType, ClientState>>,

    /// The "clients" inspect node which is nested under the "SystemPowerModeHandler" inspect node.
    /// Each `ClientState` gets its own child under this node.
    clients_inspect: inspect::Node,
}

impl ClientStates {
    /// Creates a new `ClientStates` instance based on the provided `SystemPowerModeConfig`.
    ///
    /// The `ClientStates` instance will initially hold a `ClientState` entry for each configured
    /// client in the provided `SystemPowerModeConfig`. If an additional client is configured later
    /// (via `fuchsia.power.systemmode/ClientConfigurator`) then a `ClientState` entry can be
    /// created and added at that time.
    fn new(
        system_power_mode_config: SystemPowerModeConfig,
        clients_inspect: inspect::Node,
    ) -> Self {
        let mut states = HashMap::new();
        system_power_mode_config.into_iter().for_each(|(client_type, config)| {
            states.insert(client_type, ClientState::new(client_type, config, &clients_inspect));
        });

        ClientStates { states: RefCell::new(states), clients_inspect }
    }

    /// Processes the new set of system power modes for each client.
    ///
    /// This function takes the new set of system power modes and simply passes it through to each
    /// contained `ClientState` entry.
    fn process_system_power_modes_changed(&self, system_power_modes: &HashSet<SystemMode>) {
        fuchsia_trace::duration!(
            "power_manager",
            "SystemPowerModeHandler::process_system_power_modes_changed"
        );

        self.states.borrow_mut().values_mut().for_each(|client_state| {
            client_state.process_system_power_modes_changed(system_power_modes)
        });
    }

    /// Connects a `fuchsia.power.clientlevel.Watcher` request stream to a specific client type.
    ///
    /// If successful, the incoming `Watch` requests on the stream will be completed with the power
    /// level of the given `client_type` as it changes.
    fn connect_stream_for_client(
        &self,
        client_type: ClientType,
        stream: fpowerclient::WatcherRequestStream,
    ) -> Result<(), Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "SystemPowerModeHandler::connect_stream_for_client"
        );

        match self.states.borrow_mut().get_mut(&client_type) {
            Some(client_state) => {
                client_state.connect_stream(stream);
                Ok(())
            }
            None => Err(format_err!("Unsupported client type: {:?}", client_type)),
        }
    }

    /// Gets the `ClientConfig` for `ClientType` and passes it to the provided `get_fn` closure.
    ///
    /// The `get_fn` closure approach is used to help with ownership ergonomics.
    fn get_config(&self, client_type: ClientType, get_fn: impl FnOnce(Option<&mut ClientConfig>)) {
        get_fn(self.states.borrow_mut().get_mut(&client_type).map(|state| &mut state.config))
    }

    /// Updates the `ClientConfig` for `ClientType` then reevalutes their power level.
    ///
    /// If there was not a previous `ClientState` entry for the provided `ClientType` then one is
    /// first created and added to the `self.states` map. After the new configuration is updated,
    /// the client's power level is reevaluated using the provided `system_power_modes`.
    fn update_config(
        &self,
        client_type: ClientType,
        config: ClientConfig,
        system_power_modes: &HashSet<SystemMode>,
    ) {
        let mut states = self.states.borrow_mut();
        let client_state = if let Some(client_state) = states.get_mut(&client_type) {
            client_state.client_state_inspect.set_config(&config);
            client_state.config = config;
            client_state
        } else {
            states
                .insert(client_type, ClientState::new(client_type, config, &self.clients_inspect));
            states.get_mut(&client_type).unwrap()
        };

        client_state.process_system_power_modes_changed(system_power_modes);
    }
}

/// Stores the configuration and state for a single client.
struct ClientState {
    /// The system power mode configuration that determines this client's power level as a function
    /// of the currently active system power modes.
    config: ClientConfig,

    /// We pass new power level values to the publisher, which takes care of updating the remote
    /// client using hanging-gets.
    publisher: PowerLevelPublisher,

    /// We use the broker to vend a new `PowerLevelSubscriber` for each new `Watcher` request
    /// stream.
    broker: PowerLevelBroker,

    /// Cached power level value. Simply used to determine if the value has changed.
    power_level: u64,

    /// Structure to track and own Inspect data for the `ClientState`, which is nested under the
    /// "clients" inspect node.
    client_state_inspect: ClientStateInspect,
}

impl ClientState {
    fn new(client_type: ClientType, config: ClientConfig, clients_root: &inspect::Node) -> Self {
        let broker = new_power_level_broker();
        let client_state_inspect = ClientStateInspect::new(
            clients_root.create_child(format!("{:?}", client_type).to_lowercase()),
        );
        client_state_inspect.set_config(&config);

        Self {
            publisher: broker.new_publisher(),
            broker,
            power_level: u64::MAX,
            client_state_inspect,
            config,
        }
    }

    /// Connects a new `Watcher` request stream to this client's power level.
    fn connect_stream(&mut self, stream: fpowerclient::WatcherRequestStream) {
        self.client_state_inspect.connect_count.add(1);
        spawn_watcher_handler(stream, self.broker.new_subscriber());
    }

    /// Processes the new set of system power modes.
    ///
    /// The new power level is determined according to the new system power modes. If the new power
    /// level has changed, then the new value is passed to the publisher, where the remote client
    /// will see the new value.
    fn process_system_power_modes_changed(&mut self, system_power_modes: &HashSet<SystemMode>) {
        let new_power_level = self.determine_power_level(system_power_modes);
        if new_power_level != self.power_level {
            self.power_level = new_power_level;
            self.client_state_inspect.power_level.set(new_power_level);
            self.publisher.set(new_power_level);
        }
    }

    /// Determines the power level according to the provided set of system power modes.
    ///
    /// The function will iterate through the configured `mode_match` entries. If a `mode_match`
    /// entry is found which specifies a `SystemMode` which is contained by `system_power_modes`,
    /// then the corresponding `power_level` is returned. If there is no match, then `default_level`
    /// is returned.
    fn determine_power_level(&self, system_power_modes: &HashSet<SystemMode>) -> u64 {
        for mode_match in &self.config.mode_matches {
            if system_power_modes.contains(&mode_match.mode) {
                return mode_match.power_level;
            }
        }

        self.config.default_level
    }
}

/// Spawns a `Task` to handle `Watch` requests from a `fuchsia.power.clientlevel.Watcher` channel.
///
/// The `Watch` requests will be fulfilled by registering them with the provided `subscriber`. The
/// `subscriber` is tied to the power level for a specific client type. Therefore, the `Watch`
/// requests will be responded to with the power level of the specific client type of `subscriber`.
fn spawn_watcher_handler(
    mut stream: fpowerclient::WatcherRequestStream,
    subscriber: PowerLevelSubscriber,
) {
    fuchsia_trace::duration!("power_manager", "SystemPowerModeHandler::spawn_watcher_handler");

    fasync::Task::local(
        async move {
            while let Some(fpowerclient::WatcherRequest::Watch { responder }) =
                stream.try_next().await?
            {
                fuchsia_trace::duration!(
                    "power_manager",
                    "SystemPowerModeHandler::spawn_watcher_handler::Watch"
                );

                // The responder for the `Watch` FIDL request is now owned by the subscriber. The
                // request will be completed with the new power level once it is ready.
                subscriber.register(responder)?
            }

            Ok(())
        }
        .unwrap_or_else(|e: anyhow::Error| error!("{:?}", e)),
    )
    .detach();
}

/// Holds inspect properties for the top-level "SystemPowerModeHandler" inspect node.
struct SystemPowerModeHandlerInspect {
    _root: inspect::Node,
    system_power_modes: inspect::StringProperty,
}

impl SystemPowerModeHandlerInspect {
    fn new(root: inspect::Node) -> Self {
        let system_power_modes = root.create_string("system_power_modes", "");
        Self { _root: root, system_power_modes }
    }

    fn set_power_modes(&self, system_power_modes: &HashSet<SystemMode>) {
        self.system_power_modes.set(format!("{:?}", system_power_modes).as_str())
    }
}

/// A structure to own and track Inspect data for a single `ClientState` instance.
struct ClientStateInspect {
    power_level: inspect::UintProperty,

    // TODO(fxbug.dev/93970): track # of active connections instead of just connect count
    connect_count: inspect::UintProperty,
    config: inspect::StringProperty,
    _client_node: inspect::Node,
}

impl ClientStateInspect {
    fn new(client_node: inspect::Node) -> Self {
        let power_level = client_node.create_uint("power_level", u64::MAX);
        let connect_count = client_node.create_uint("connect_count", 0);
        let config = client_node.create_string("config", "");
        Self { power_level, connect_count, config, _client_node: client_node }
    }

    fn set_config(&self, config: &ClientConfig) {
        self.config.set(format!("{:?}", config).as_str());
    }
}

// Below are a series of type aliases for convenience
type WatchResponder = fpowerclient::WatcherWatchResponder;
type PowerLevelChangeFn = Box<dyn Fn(&u64, WatchResponder) -> bool>;
type PowerLevelBroker = hanging_get::HangingGet<u64, WatchResponder, PowerLevelChangeFn>;
type PowerLevelPublisher = hanging_get::Publisher<u64, WatchResponder, PowerLevelChangeFn>;
type PowerLevelSubscriber = hanging_get::Subscriber<u64, WatchResponder, PowerLevelChangeFn>;

/// Convenience function to create a new `PowerLevelBroker` instance.
///
/// The broker is used to vend new `PowerLevelPublisher` and `PowerLevelSubscriber` instances.
///
/// When `PowerLevelPublisher.set()` is called with a power level value, then all pending `Watch`
/// requests registered with a corresponding `PowerLevelSubscriber` (see the `spawn_watcher_handler`
/// function) will be completed with that value, regardless of whether that value differs from the
/// previous power level that was sent to the client. Therefore, care should be taken to only call
/// `PowerLevelPublisher.set()` when the power level value has actually changed in order to properly
/// implement the hanging-get behavior.
fn new_power_level_broker() -> PowerLevelBroker {
    let notify_fn: PowerLevelChangeFn = Box::new(|power_level, responder| {
        match responder.send(*power_level) {
            Ok(()) => true, // indicates that the client was successfully updated
            Err(e) => {
                error!("Failed to send power level to client: {}", e);
                false
            }
        }
    });
    hanging_get::HangingGet::new(u64::MAX, notify_fn)
}

impl SystemPowerModeHandler {
    /// Publishes the following services:
    ///  - fuchsia.power.clientlevel.Connector
    ///  - fuchsia.power.systemmode.Requester
    ///  - fuchsia.power.systemmode.ClientConfigurator
    ///
    /// For each new connection on any of the services, the appropriate handler function is called
    /// on a clone of the SystemPowerModeHandler node and provided with the request stream.
    fn publish_services<'a, 'b>(
        self: Rc<Self>,
        mut outgoing_svc_dir: ServiceFsDir<'a, ServiceObjLocal<'b, ()>>,
    ) {
        let clone = self.clone();
        outgoing_svc_dir.add_fidl_service(move |stream| {
            clone.clone().spawn_client_connector_handler(stream);
        });

        let clone = self.clone();
        outgoing_svc_dir.add_fidl_service(move |stream| {
            clone.clone().spawn_mode_requester_handler(stream);
        });

        let clone = self.clone();
        outgoing_svc_dir.add_fidl_service(move |stream| {
            clone.clone().spawn_configurator_handler(stream);
        });
    }

    /// Spawns a `Task` to handle `Connect` requests from a `fuchsia.power.clientlevel.Connector`
    /// channel.
    ///
    /// The `Connect` requests contain a `client_type` value and a
    /// `fuchsia.power.clientlevel.Watcher` server end. If the request is valid, then the `Watch`
    /// requests on the provided `Watcher` server end will be responded to with the power level of
    /// the given `client_type`.
    fn spawn_client_connector_handler(
        self: Rc<Self>,
        mut stream: fpowerclient::ConnectorRequestStream,
    ) {
        fuchsia_trace::duration!(
            "power_manager",
            "SystemPowerModeHandler::spawn_connector_handler"
        );

        fasync::Task::local(
            async move {
                while let Some(req) = stream.try_next().await? {
                    match req {
                        fpowerclient::ConnectorRequest::Connect {
                            client_type, watcher, ..
                        } => self
                            .client_states
                            .connect_stream_for_client(client_type, watcher.into_stream()?)?,
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| error!("{:?}", e)),
        )
        .detach();
    }

    /// Spawns a `Task` to handle `Request` requests to modify the currently active system power
    /// modes from a `fuchsia.power.systemmode.Requester` channel.
    ///
    /// If the request is valid, then the system power modes are updated and the clients' power
    /// levels are reevaluated.
    fn spawn_mode_requester_handler(
        self: Rc<Self>,
        mut stream: fpowermode::RequesterRequestStream,
    ) {
        fuchsia_trace::duration!(
            "power_manager",
            "SystemPowerModeHandler::spawn_connector_handler"
        );

        fasync::Task::local(
            async move {
                while let Some(req) = stream.try_next().await? {
                    match req {
                        fpowermode::RequesterRequest::Request { mode, set, responder } => {
                            let mut result =
                                self.modify_system_power_mode(mode, set).and_then(|_| {
                                    self.client_states.process_system_power_modes_changed(
                                        &self.system_power_modes.borrow(),
                                    );
                                    Ok(())
                                });

                            log_if_err!(
                                responder.send(&mut result),
                                "Failed to send power mode request response"
                            );
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| error!("{:?}", e)),
        )
        .detach();
    }

    /// Spawns a `Task` to handle `Get` and `Set` requests from a
    /// `fuchsia.power.systemmode.ClientConfigurator` channel.
    fn spawn_configurator_handler(
        self: Rc<Self>,
        mut stream: fpowermode::ClientConfiguratorRequestStream,
    ) {
        fuchsia_trace::duration!(
            "power_manager",
            "SystemPowerModeHandler::spawn_connector_handler"
        );

        fasync::Task::local(
            async move {
                while let Some(req) = stream.try_next().await? {
                    match req {
                        fpowermode::ClientConfiguratorRequest::Get { client_type, responder } => {
                            self.client_states.get_config(client_type, |config| {
                                log_if_err!(
                                    responder.send(config),
                                    "Failed to send ClientConfigurator.Get response"
                                );
                            });
                        }
                        fpowermode::ClientConfiguratorRequest::Set {
                            client_type,
                            config,
                            responder,
                        } => {
                            config.validate()?;
                            log_if_err!(
                                responder.send(),
                                "Failed to send ClientConfigurator.Set response"
                            );

                            self.client_states.update_config(
                                client_type,
                                config,
                                &self.system_power_modes.borrow(),
                            );
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| error!("{:?}", e)),
        )
        .detach();
    }

    /// Modifies the currently active system power modes.
    fn modify_system_power_mode(
        &self,
        mode: SystemMode,
        set: bool,
    ) -> Result<(), fpowermode::ModeRequestError> {
        let mut modes = self.system_power_modes.borrow_mut();

        if set {
            modes.insert(mode);
        } else {
            modes.remove(&mode);
        }

        self.inspect.set_power_modes(&modes);

        Ok(())
    }
}

impl Node for SystemPowerModeHandler {
    fn name(&self) -> String {
        "SystemPowerModeHandler".to_string()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use fuchsia_inspect::assert_data_tree;
    use std::task::Poll;
    use system_power_mode_config::{ClientConfigTestExt, SystemPowerModeConfigTestExt};

    // Takes a `ServiceFs` which contains the `SystemPowerModeHandler` node's implementation of the
    // three power protocols and manages the underlying test infrastructure required to connect to
    // and use the services.
    struct TestEnv {
        env: fuchsia_component::server::NestedEnvironment,
    }

    impl TestEnv {
        // Takes a ServiceFs and creates a nested environment which we'll later use for connecting
        // to the `SystemPowerModeHandler` node's services.
        fn new(mut service_fs: ServiceFs<ServiceObjLocal<'static, ()>>) -> Self {
            let env = service_fs.create_nested_environment("env").unwrap();
            fasync::Task::local(service_fs.collect()).detach();
            Self { env }
        }

        // Makes a `FakeClient` by first connecting to the `fuchsia.power.clientlevel.Connector`
        // protocol contained within the `NestedEnvironment` and connecting a
        // `fuchsia.power.clientlevel.Watcher` server end of the given `client_type`.
        fn make_fake_client(&self, client_type: ClientType) -> FakeClient {
            let connector =
                self.env.connect_to_protocol::<fpowerclient::ConnectorMarker>().unwrap();

            let (watcher_proxy, watcher_server_end) =
                fidl::endpoints::create_proxy::<fpowerclient::WatcherMarker>().unwrap();

            // Pass the `watcher_server_end` to the node, so it will be associated with power level
            // changes of `client_type`
            assert_matches!(connector.connect(client_type, watcher_server_end), Ok(()));

            FakeClient { watcher_proxy, hanging_watcher_request: RefCell::new(None) }
        }

        // Makes a `FakeConfigurator` by connecting to the
        // `fuchsia.power.systemmode.ClientConfigurator` protocol contained within the
        // `NestedEnvironment`.
        fn make_fake_configurator(&self) -> FakeConfigurator {
            let proxy =
                self.env.connect_to_protocol::<fpowermode::ClientConfiguratorMarker>().unwrap();

            FakeConfigurator { proxy }
        }
    }

    // A fake client for watching a client's power level changes from the `SystemPowerModeHandler`.
    struct FakeClient {
        watcher_proxy: fpowerclient::WatcherProxy,
        hanging_watcher_request: RefCell<Option<fidl::client::QueryResponseFut<u64>>>,
    }

    impl FakeClient {
        // Gets the power level for the fake client.
        //
        // Since requests are using hanging-get, there are three possible return values to consider:
        //  - Ok(None) = the watch request succeeded but there are no updates to the client's power
        //     level (the request is now "hanging")
        //  - Ok(Some(state)) = the watch request succeeded and returned a new power level
        //  - Err(e) = the watch request failed
        fn get_power_level(
            &self,
            executor: &mut fasync::TestExecutor,
        ) -> Result<Option<u64>, Error> {
            // If there's already a hanging request (the previous call to `get_power_level` returned
            // `Ok(None)`), then check if that request has a response for us. If there wasn't
            // already a hanging request, then send a new one on the channel
            let mut watch_request = self
                .hanging_watcher_request
                .take() // take the Option from the RefCell
                .take() // take the pending request (if any) from the Option
                .unwrap_or_else(|| self.watcher_proxy.watch());

            match executor.run_until_stalled(&mut watch_request) {
                Poll::Pending => {
                    // The request is now "hanging" with the server. Cache it so we can check it for
                    // a response in subsequent calls to `get_power_level`.
                    self.hanging_watcher_request.replace(Some(watch_request));
                    Ok(None)
                }
                Poll::Ready(Ok(power_level)) => Ok(Some(power_level)),
                Poll::Ready(Err(e)) => Err(e.into()),
            }
        }
    }

    // A fake client configurator for reconfiguring a client's power configuration using the
    // `fuchsia.power.systemmode.ClientConfigurator` protocol.
    struct FakeConfigurator {
        proxy: fpowermode::ClientConfiguratorProxy,
    }

    impl FakeConfigurator {
        fn get_config(
            &self,
            executor: &mut fasync::TestExecutor,
            client_type: ClientType,
        ) -> Option<ClientConfig> {
            executor.run_singlethreaded(self.proxy.get(client_type)).unwrap().map(|v| *v)
        }

        fn set_config(
            &self,
            executor: &mut fasync::TestExecutor,
            client_type: ClientType,
            mut config: ClientConfig,
        ) {
            assert_matches!(
                executor.run_singlethreaded(self.proxy.set(client_type, &mut config)),
                Ok(())
            );
        }
    }

    /// Tests that well-formed configuration JSON does not panic the `new_from_json` function.
    #[test]
    fn test_new_from_json() {
        let json_data = json::json!({
            "type": "SystemPowerModeHandler",
            "name": "system_power_level_handler"
        });
        let _ = SystemPowerModeHandlerBuilder::new_from_json(
            json_data,
            &HashMap::new(),
            &mut ServiceFs::new_local(),
        );
    }

    /// Tests that a client's state (including power level, connect count, and configuration) is
    /// correctly published into Inspect.
    #[test]
    fn test_inspect() {
        let mut executor = fasync::TestExecutor::new().unwrap();
        let mut service_fs = ServiceFs::new_local();

        // Create a test config with a `Wlan` client whose default power level is 1
        let system_power_mode_config =
            SystemPowerModeConfig::new().add_client_config(ClientType::Wlan, ClientConfig::new(1));

        let inspector = inspect::Inspector::new();
        let _node = SystemPowerModeHandlerBuilder::new()
            .with_inspect_root(inspector.root())
            .with_system_power_mode_config(system_power_mode_config)
            .with_outgoing_svc_dir(service_fs.root_dir())
            .build()
            .unwrap();

        let test_env = TestEnv::new(service_fs);

        // Check for default initialized values
        assert_data_tree!(
            inspector,
            root: {
                SystemPowerModeHandler: {
                    clients: {
                        wlan: {
                            power_level: 1u64,
                            connect_count: 0u64,
                            config: "ClientConfig { mode_matches: [], default_level: 1 }"
                        }
                    },
                    system_power_modes: ""
                }
            }
        );

        // Connect two instances of a Wlan client and verify the `connect_count` is now 2
        let client1 = test_env.make_fake_client(ClientType::Wlan);
        assert_matches!(client1.get_power_level(&mut executor), Ok(Some(1)));

        let client2 = test_env.make_fake_client(ClientType::Wlan);
        assert_matches!(client2.get_power_level(&mut executor), Ok(Some(1)));

        assert_data_tree!(
            inspector,
            root: {
                SystemPowerModeHandler: {
                    clients: {
                        wlan: {
                            power_level: 1u64,
                            connect_count: 2u64,
                            config: "ClientConfig { mode_matches: [], default_level: 1 }"
                        }
                    },
                    system_power_modes: ""
                }
            }
        );

        // Reconfigure the Wlan `default_level` to 2 then verify its `power_level` shows 2
        let fake_configurator = test_env.make_fake_configurator();
        fake_configurator.set_config(&mut executor, ClientType::Wlan, ClientConfig::new(2));

        assert_matches!(client1.get_power_level(&mut executor), Ok(Some(2)));
        assert_matches!(client2.get_power_level(&mut executor), Ok(Some(2)));

        assert_data_tree!(
            inspector,
            root: {
                SystemPowerModeHandler: {
                    clients: {
                        wlan: {
                            power_level: 2u64,
                            connect_count: 2u64,
                            config: "ClientConfig { mode_matches: [], default_level: 2 }"
                        }
                    },
                    system_power_modes: ""
                }
            }
        );
    }

    /// Tests that the fuchsia.power.clientlevel.Watcher server correctly implements the hanging-get
    /// pattern.
    #[test]
    fn test_hanging_get() {
        let mut executor = fasync::TestExecutor::new().unwrap();
        let mut service_fs = ServiceFs::new_local();

        // Create a test config with a `Wlan` client whose default power level is 0
        let system_power_mode_config =
            SystemPowerModeConfig::new().add_client_config(ClientType::Wlan, ClientConfig::new(0));

        let _node = SystemPowerModeHandlerBuilder::new()
            .with_system_power_mode_config(system_power_mode_config)
            .with_outgoing_svc_dir(service_fs.root_dir())
            .build()
            .unwrap();

        let test_env = TestEnv::new(service_fs);

        // Connect the client
        let client = test_env.make_fake_client(ClientType::Wlan);

        // First request gives initial power level of 0
        assert_matches!(client.get_power_level(&mut executor), Ok(Some(0)));

        // Second request has no update
        assert_matches!(client.get_power_level(&mut executor), Ok(None));

        // Now update the Wlan default power level to 1
        let fake_configurator = test_env.make_fake_configurator();
        fake_configurator.set_config(&mut executor, ClientType::Wlan, ClientConfig::new(1));

        // Verify the client now gets the new power level of 1
        assert_matches!(client.get_power_level(&mut executor), Ok(Some(1)));

        // Update the Wlan default power level to 1 again (no change)
        fake_configurator.set_config(&mut executor, ClientType::Wlan, ClientConfig::new(1));

        // Verify there is no new power level for the client
        assert_matches!(client.get_power_level(&mut executor), Ok(None));
    }

    /// Tests that a connect request for an unconfigured `client_type` returns an error.
    #[test]
    fn test_unsupported_client() {
        let mut executor = fasync::TestExecutor::new().unwrap();
        let mut service_fs = ServiceFs::new_local();

        let _node = SystemPowerModeHandlerBuilder::new()
            .with_system_power_mode_config(SystemPowerModeConfig::new())
            .with_outgoing_svc_dir(service_fs.root_dir())
            .build()
            .unwrap();

        let test_env = TestEnv::new(service_fs);
        let client = test_env.make_fake_client(ClientType::Wlan);

        // Connect a client for Wlan, which is not specified in our `SystemPowerModeConfig`
        assert_matches!(client.get_power_level(&mut executor), Err(_));
    }

    /// Tests that multiple clients connected simultaneously receive their expected power level
    /// updates separately.
    #[test]
    fn test_multiple_client_types() {
        let mut executor = fasync::TestExecutor::new().unwrap();
        let mut service_fs = ServiceFs::new_local();

        // Create a test config with a `Wlan` client whose default power level is 0
        let system_power_mode_config =
            SystemPowerModeConfig::new().add_client_config(ClientType::Wlan, ClientConfig::new(0));

        let _node = SystemPowerModeHandlerBuilder::new()
            .with_system_power_mode_config(system_power_mode_config)
            .with_outgoing_svc_dir(service_fs.root_dir())
            .build()
            .unwrap();

        let test_env = TestEnv::new(service_fs);

        // Connect two Wlan clients
        let client1 = test_env.make_fake_client(ClientType::Wlan);
        let client2 = test_env.make_fake_client(ClientType::Wlan);

        // First request gives the initial default power level for both clients
        assert_matches!(client1.get_power_level(&mut executor), Ok(Some(0)));
        assert_matches!(client2.get_power_level(&mut executor), Ok(Some(0)));

        // Now update the default power level to 1
        let fake_configurator = test_env.make_fake_configurator();
        fake_configurator.set_config(&mut executor, ClientType::Wlan, ClientConfig::new(1));

        // Verify client1 gets the new power level, then no update on the second call
        assert_matches!(client1.get_power_level(&mut executor), Ok(Some(1)));
        assert_matches!(client1.get_power_level(&mut executor), Ok(None));

        // Verify client2 gets the new power level, then no update on the second call
        assert_matches!(client2.get_power_level(&mut executor), Ok(Some(1)));
        assert_matches!(client2.get_power_level(&mut executor), Ok(None));
    }

    /// Tests that calling `fuchsia.power.systemmode/ClientConfigurator.Get` for an unconfigured
    /// `ClientType` returns a missing config.
    #[test]
    fn test_get_missing_client_config() {
        let mut executor = fasync::TestExecutor::new().unwrap();
        let mut service_fs = ServiceFs::new_local();

        // Configuration with no configured clients
        let system_power_mode_config = SystemPowerModeConfig::new();

        let _node = SystemPowerModeHandlerBuilder::new()
            .with_system_power_mode_config(system_power_mode_config)
            .with_outgoing_svc_dir(service_fs.root_dir())
            .build()
            .unwrap();

        let test_env = TestEnv::new(service_fs);

        // Try to get the config for Wlan and verify it is missing
        let fake_configurator = test_env.make_fake_configurator();
        let config = fake_configurator.get_config(&mut executor, ClientType::Wlan);
        assert_eq!(config, None);
    }

    /// Tests that calling `fuchsia.power.systemmode/ClientConfigurator.Get` for a configured
    /// `ClientType` returns the correct config.
    #[test]
    fn test_get_present_client_config() {
        let mut executor = fasync::TestExecutor::new().unwrap();
        let mut service_fs = ServiceFs::new_local();

        // Create a test config with a `Wlan` client whose default power level is 0
        let system_power_mode_config =
            SystemPowerModeConfig::new().add_client_config(ClientType::Wlan, ClientConfig::new(0));

        let _node = SystemPowerModeHandlerBuilder::new()
            .with_system_power_mode_config(system_power_mode_config)
            .with_outgoing_svc_dir(service_fs.root_dir())
            .build()
            .unwrap();

        let test_env = TestEnv::new(service_fs);

        // Get the config for Wlan and verify it matches the expected config
        let fake_configurator = test_env.make_fake_configurator();
        let config = fake_configurator.get_config(&mut executor, ClientType::Wlan);
        assert_eq!(config, Some(ClientConfig::new(0)));
    }

    /// Tests that `fuchsia.power.systemmode/ClientConfigurator.Set` correctly reconfigures a
    /// client.
    #[test]
    fn test_set_client_config() {
        let mut executor = fasync::TestExecutor::new().unwrap();
        let mut service_fs = ServiceFs::new_local();

        // Start with an empty config
        let system_power_mode_config = SystemPowerModeConfig::new();

        let _node = SystemPowerModeHandlerBuilder::new()
            .with_system_power_mode_config(system_power_mode_config)
            .with_outgoing_svc_dir(service_fs.root_dir())
            .build()
            .unwrap();

        let test_env = TestEnv::new(service_fs);

        // Verify the config is initially empty
        let fake_configurator = test_env.make_fake_configurator();
        let config = fake_configurator.get_config(&mut executor, ClientType::Wlan);
        assert_eq!(config, None);

        // Set a new config
        fake_configurator.set_config(&mut executor, ClientType::Wlan, ClientConfig::new(1));

        // Verify the new config is now returned
        let config = fake_configurator.get_config(&mut executor, ClientType::Wlan);
        assert_eq!(config, Some(ClientConfig::new(1)));
    }
}
