// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::PowerManagerError;
use crate::log_if_err;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::system_shutdown_handler;
use crate::types::Seconds;
use crate::utils::{get_current_timestamp, result_debug_panic::ResultDebugPanic};
use anyhow::{format_err, Context, Error};
use async_trait::async_trait;
use async_utils::event::Event as AsyncEvent;
use fidl::endpoints::Proxy;
use fidl_fuchsia_device_manager as fdevicemgr;
use fidl_fuchsia_hardware_power_statecontrol as fpowerstatecontrol;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_power_manager as fpowermanager;
use fuchsia_async::{self as fasync, DurationExt, TimeoutExt};
use fuchsia_component::server::{ServiceFs, ServiceFsDir, ServiceObjLocal};
use fuchsia_inspect::{self as inspect, Property};
use fuchsia_inspect_contrib::{inspect_log, nodes::BoundedListNode};
use fuchsia_zircon as zx;
use futures::channel::mpsc;
use futures::prelude::*;
use futures::TryStreamExt;
use log::*;
use serde_derive::Deserialize;
use serde_json as json;
use std::cell::RefCell;
use std::collections::HashMap;
use std::rc::Rc;

/// Node: DriverManagerHandler
///
/// Summary: The primary purpose of this node is to host the
/// fuchsia.power.manager.DriverManagerRegistration service. The Driver Manager will use this
/// service to register protocol instances that Power Manager requires for normal operation. After
/// the registration protocol instances are received, the node is responsible for:
///     1) Monitoring the provided fuchsia.device.manager.SystemStateTransition protocol instance
///        for closure.
///     2) Vending channel connections to the underlying services in the provided
///        fuchsia.io.Directory protocol instance
///     3) Setting the termination system state on the Driver Manager
///
/// Handles Messages:
///     - SetTerminationSystemState
///
/// Sends Messages: N/A
///
/// FIDL dependencies:
///     - fuchsia.power.manager.DriverManagerRegistration: the node hosts this protocol to allow the
///       Driver Manager to register essential protocol instances with the Power Manager
///     - fuchsia.device.manager.SystemStateTransition: a protocol of this instance is provided to
///       the Power Manager by the Driver Manager using the
///       fuchsia.power.manager.DriverManagerRegistration protocol. The SystemStateTransition is
///       then used to set the Driver Manager's termination system state.
///     - fuchsia.io.Directory: a protocol of this instance is provided to the Power Manager by the
///       Driver Manager using the fuchsia.power.manager.DriverManagerRegistration protocol. The
///       Directory is expected to represent the devfs (/dev) and is used to open driver
///       connections.

/// A builder for constructing the DriverManagerHandler node.
pub struct DriverManagerHandlerBuilder<'a, 'b> {
    registration_timeout: Option<Seconds>,
    driver_manager_registration: Option<DriverManagerRegistration>,
    termination_channel_closed_handler: Option<Box<dyn FnOnce()>>,
    outgoing_svc_dir: Option<ServiceFsDir<'a, ServiceObjLocal<'b, ()>>>,
    inspect_root: Option<&'a inspect::Node>,
}

impl<'a, 'b> DriverManagerHandlerBuilder<'a, 'b> {
    #[cfg(test)]
    fn new() -> Self {
        Self {
            registration_timeout: None,
            driver_manager_registration: None,
            termination_channel_closed_handler: None,
            outgoing_svc_dir: None,
            inspect_root: None,
        }
    }

    #[cfg(test)]
    pub fn outgoing_svc_dir(mut self, dir: ServiceFsDir<'a, ServiceObjLocal<'b, ()>>) -> Self {
        self.outgoing_svc_dir = Some(dir);
        self
    }

    #[cfg(test)]
    fn registration_timeout(mut self, timeout: Seconds) -> Self {
        self.registration_timeout = Some(timeout);
        self
    }

    #[cfg(test)]
    fn termination_channel_closed_handler(mut self, handler: Box<impl FnOnce() + 'static>) -> Self {
        self.termination_channel_closed_handler = Some(handler);
        self
    }

    #[cfg(test)]
    fn driver_manager_registration(mut self, registration: DriverManagerRegistration) -> Self {
        self.driver_manager_registration = Some(registration);
        self
    }

    #[cfg(test)]
    fn inspect_root(mut self, inspect_root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(inspect_root);
        self
    }

    pub fn new_from_json(
        json_data: json::Value,
        _nodes: &HashMap<String, Rc<dyn Node>>,
        service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
    ) -> Self {
        #[derive(Deserialize)]
        struct Config {
            registration_timeout_s: Option<f64>,
        }

        #[derive(Deserialize)]
        struct JsonData {
            config: Config,
        }

        let data: JsonData = json::from_value(json_data).unwrap();

        Self {
            registration_timeout: data.config.registration_timeout_s.map(Seconds),
            driver_manager_registration: None,
            termination_channel_closed_handler: None,
            outgoing_svc_dir: Some(service_fs.dir("svc")),
            inspect_root: None,
        }
    }

    pub fn build(self) -> Result<Rc<DriverManagerHandler>, Error> {
        // Default `termination_channel_closed_handler` calls
        // `system_shutdown_handler::force_shutdown()` to force a system reboot
        let termination_channel_closed_handler = self
            .termination_channel_closed_handler
            .unwrap_or(Box::new(default_termination_channel_closed_handler));

        // Optionally use the default inspect root node
        let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());

        // Create the channel pair that will be used to send the registration from the sender end in
        // the FIDL server task to the receiver end in `init()`. If
        // `self.driver_manager_registration` is populated by a test, then we can go ahead and place
        // it into the channel to be consumed during `init()`.
        let (mut registration_sender, registration_receiver) = futures::channel::mpsc::channel(1);
        if let Some(registration) = self.driver_manager_registration {
            registration_sender.try_send(registration)?;
        }

        // Create a directory channel to initially bind at "/dev" without connecting the remote end
        // to anything (yet). Requests made to "/dev" will be queued up in the channel until the
        // remote end (`local_devfs_server`) is connected/served. This is necessary so if other
        // nodes try to connect to a driver before we've received registration, the watcher APIs
        // will still work and wait as expected (if there was no "/dev" directory then the watcher
        // APIs would fail). In our init() function, we take the DevFs channel that Driver Manager
        // provides via registration and connect it to `local_devfs_server`, then any queued up
        // requests will be passed through.
        let (local_devfs_client, local_devfs_server) = fidl::endpoints::create_endpoints()?;
        bind_driver_directory(local_devfs_client).context("Failed to bind driver directory")?;

        // Set up Inspect and log the registration timeout configuration
        let inspect = InspectData::new(inspect_root, "DriverManagerHandler".to_string());
        inspect
            .registration_timeout_config
            .set(self.registration_timeout.unwrap_or(Seconds(0.0)).0 as u64);

        let mutable_inner = MutableInner {
            registration_receiver,
            termination_channel_closed_handler: Some(termination_channel_closed_handler),
            termination_state_proxy: None,
            monitor_termination_channel_closed_task: None,
            local_devfs_server: Some(local_devfs_server),
        };

        let node = Rc::new(DriverManagerHandler {
            init_done: AsyncEvent::new(),
            registration_timeout: self.registration_timeout,
            mutable_inner: RefCell::new(mutable_inner),
            inspect,
        });

        if let Some(outgoing_svc_dir) = self.outgoing_svc_dir {
            publish_registration_service(registration_sender, outgoing_svc_dir);
        }

        Ok(node)
    }

    #[cfg(test)]
    pub async fn build_and_init(self) -> Rc<DriverManagerHandler> {
        let node = self.build().unwrap();
        node.init().await.unwrap();
        node
    }
}

struct MutableInner {
    /// Receiver to emit valid DriverManagerRegistration. Will be waited on during `init()`. The
    /// sender end is owned by the FIDL server task.
    registration_receiver: mpsc::Receiver<DriverManagerRegistration>,

    /// Function to call if the `termination_state_proxy` channel is closed. Initially populated,
    /// but later moved into `monitor_termination_channel_closed_task`.
    termination_channel_closed_handler: Option<Box<dyn FnOnce()>>,

    /// Proxy instance that the Power Manager uses to set the Driver Manager's termination system
    /// state. Populated during `init()`.
    termination_state_proxy: Option<fdevicemgr::SystemStateTransitionProxy>,

    /// Task that monitors for closure of `termination_state_proxy` to call
    /// `termination_channel_closed_handler`. Populated once we have a valid
    /// `termination_state_proxy` during `init()`.
    monitor_termination_channel_closed_task: Option<fasync::Task<()>>,

    /// Remote end of the directory channel bound to our namespace at "/dev". In our init()
    /// function, this remote end will be connected to the DevFs channel provided by Driver Manager
    /// so that other nodes can access drivers via "/dev" as expected.
    local_devfs_server: Option<fidl::endpoints::ServerEnd<fio::DirectoryMarker>>,
}

/// Default handler function that will be called if the fuchsia.device.manager.SystemStateTransition
/// protocol instance that was provided during registration is ever closed.
fn default_termination_channel_closed_handler() {
    error!("SystemStateTransition channel closed. Forcing system shutdown");
    fuchsia_trace::instant!(
        "power_manager",
        "DriverManagerHandler::termination_channel_closed_handler",
        fuchsia_trace::Scope::Thread
    );
    system_shutdown_handler::force_shutdown();
}

/// Contains the required protocol instances that are received from the Driver Manager.
#[derive(Debug)]
pub struct DriverManagerRegistration {
    /// Protocol instance that the Power Manager uses to set the Driver Manager's termination system
    /// state.
    termination_state_proxy: fdevicemgr::SystemStateTransitionProxy,

    /// Directory instance that represents the devfs (/dev).
    dir: fio::DirectoryProxy,
}

impl DriverManagerRegistration {
    /// Determine if the contents of this registration are valid.
    fn validate(&self) -> Result<(), Error> {
        if self.termination_state_proxy.is_closed() {
            Err(format_err!("Invalid SystemStateTransitionProxy handle"))
        } else if self.dir.is_closed() {
            Err(format_err!("Invalid DirectoryProxy handle"))
        } else {
            Ok(())
        }
    }
}

/// Creates a "/dev" directory within the namespace that is bound to the provided DirectoryProxy.
fn bind_driver_directory(
    dir: fidl::endpoints::ClientEnd<fio::DirectoryMarker>,
) -> Result<(), Error> {
    fdio::Namespace::installed()?.bind("/dev", dir).map_err(|e| e.into())
}

pub struct DriverManagerHandler {
    /// Signalled after `init()` has completed.
    init_done: AsyncEvent,

    /// Timeout value for waiting on a `DriverManagerRegistration` from `registration_receiver`. A
    /// value of None indicates waiting forever.
    registration_timeout: Option<Seconds>,

    /// Mutable inner state.
    mutable_inner: RefCell<MutableInner>,

    /// Struct for managing Component Inspection data
    inspect: InspectData,
}

impl DriverManagerHandler {
    /// Handle the SetTerminationState message. The function uses `termination_state_proxy` to set
    /// the Driver Manager's termination state.
    async fn handle_set_termination_state_message(
        &self,
        state: fpowerstatecontrol::SystemPowerState,
    ) -> Result<MessageReturn, PowerManagerError> {
        // TODO(fxbug.dev/44484): This string must live for the duration of the function because the
        // trace macro uses it when the function goes out of scope. Therefore, it must be bound here
        // and not used anonymously at the macro callsite.
        let state_str = format!("{:?}", state);
        fuchsia_trace::duration!(
            "power_manager",
            "DriverManagerHandler::handle_set_termination_state_message",
            "state" => state_str.as_str()
        );

        self.init_done.wait().await;

        // Extract `termination_state_proxy` from `mutable_inner`, returning an error (or asserting
        // in debug) if the proxy is missing
        let termination_state_proxy = self
            .mutable_inner
            .borrow()
            .termination_state_proxy
            .as_ref()
            .ok_or(format_err!("Missing termination_state_proxy"))
            .or_debug_panic()?
            .clone();

        let result = termination_state_proxy
            .set_termination_system_state(state)
            .await
            .map_err(|e| format_err!("FIDL failed: {}", e))?;

        let result = match result.map_err(|e| zx::Status::from_raw(e)) {
            Err(zx::Status::INVALID_ARGS) => Err(PowerManagerError::InvalidArgument(format!(
                "Invalid state argument: {:?}",
                state
            ))),
            Err(e) => Err(PowerManagerError::GenericError(format_err!(
                "SetTerminationSystemState failed: {}",
                e
            ))),
            Ok(()) => Ok(MessageReturn::SetTerminationSystemState),
        };

        fuchsia_trace::instant!(
            "power_manager",
            "DriverManagerHandler::handle_set_termination_state_message_result",
            fuchsia_trace::Scope::Thread,
            "result" => format!("{:?}", result).as_str()
        );

        if let Err(e) = &result {
            self.inspect.log_set_termination_error(format!("{:?}", state), format!("{:?}", e));
        } else {
            self.inspect.termination_state.set(format!("{:?}", state).as_str());
        }

        result
    }
}

/// Publishes the fuchsia.power.manager.DriverManagerRegistration service so that the Driver manager
/// can send the registration that the Power Manager requires. The service is provided with the
/// sender end of a mpsc channel so that it can send the registration back once it is received.
fn publish_registration_service<'a, 'b>(
    registration_sender: mpsc::Sender<DriverManagerRegistration>,
    mut outgoing_svc_dir: ServiceFsDir<'a, ServiceObjLocal<'b, ()>>,
) {
    outgoing_svc_dir.add_fidl_service(move |stream| {
        handle_registration_request_stream(registration_sender.clone(), stream);
    });
}

/// Handles a new connection to the fuchsia.power.manager.DriverManagerRegistration service. The
/// provided `stream` contains the requests sent to this connection. The provided
/// `registration_sender` is used to send a valid `DriverManagerRegistration` instance to the
/// receiver, which is waited on during `init()`. For each connection to the service, we handle one
/// `Register` request then close the channel regardless of result.
fn handle_registration_request_stream(
    registration_sender: mpsc::Sender<DriverManagerRegistration>,
    mut stream: fpowermanager::DriverManagerRegistrationRequestStream,
) {
    fuchsia_trace::instant!(
        "power_manager",
        "DriverManagerHandler::handle_registration_request_stream",
        fuchsia_trace::Scope::Thread
    );

    fasync::Task::local(
        async move {
            match stream.try_next().await? {
                Some(fpowermanager::DriverManagerRegistrationRequest::Register {
                    system_state_transition,
                    dir,
                    responder,
                }) => {
                    fuchsia_trace::instant!(
                        "power_manager",
                        "DriverManagerHandler::register_request",
                        fuchsia_trace::Scope::Thread
                    );

                    let mut result =
                        handle_registration(registration_sender, system_state_transition, dir);
                    log_if_err!(
                        result.map_err(|e| format!("{:?}", e)),
                        "Received invalid registration"
                    );

                    fuchsia_trace::instant!(
                        "power_manager",
                        "DriverManagerHandler::register_request_result",
                        fuchsia_trace::Scope::Thread,
                        "result" => format!("{:?}", result).as_str()
                    );

                    responder.send(&mut result).map_err(|e| e.into())
                }
                e => Err(format_err!("Invalid registration request: {:?}", e)),
            }
        }
        .unwrap_or_else(|e: anyhow::Error| error!("{:?}", e)),
    )
    .detach();
}

/// Handles the registration parameters of a received `Register` request. If valid, the
/// `DriverManagerRegistration` is sent to `registration_sender`.
fn handle_registration(
    mut registration_sender: mpsc::Sender<DriverManagerRegistration>,
    termination_state_proxy: fidl::endpoints::ClientEnd<fdevicemgr::SystemStateTransitionMarker>,
    dir: fidl::endpoints::ClientEnd<fio::DirectoryMarker>,
) -> Result<(), fpowermanager::RegistrationError> {
    let registration = DriverManagerRegistration {
        termination_state_proxy: termination_state_proxy
            .into_proxy()
            .map_err(|_| fpowermanager::RegistrationError::InvalidHandle)?,
        dir: dir.into_proxy().map_err(|_| fpowermanager::RegistrationError::InvalidHandle)?,
    };
    registration.validate().map_err(|_| fpowermanager::RegistrationError::InvalidHandle)?;

    registration_sender
        .try_send(registration)
        .map_err(|_| fpowermanager::RegistrationError::Internal)
}

#[async_trait(?Send)]
impl Node for DriverManagerHandler {
    fn name(&self) -> String {
        "DriverManagerHandler".to_string()
    }

    /// Initializes the DriverManagerHandler.
    ///
    /// Waits to receive a `DriverManagerRegistration` over the `registration_receiver` channel
    /// according to `self.registration_timeout`. If registration is not received after the timeout
    /// then an error is returned.
    ///
    /// Received registration contains two items:
    ///     1) A DevFs channel, which will be bound to the namespace to allow other parts of the
    ///     Power Manager to access drivers
    ///     2) A `SystemStateTransition` proxy, which will be monitored for closure by creating
    ///     `monitor_termination_channel_closed_task` to call `termination_channel_closed_handler`
    ///     on closure.
    ///
    async fn init(&self) -> Result<(), Error> {
        fuchsia_trace::duration!("power_manager", "DriverManagerHandler::init");

        let timeout_time = match self.registration_timeout {
            Some(timeout) => zx::Duration::from_seconds(timeout.0 as i64).after_now(),
            None => fasync::Time::INFINITE,
        };

        let DriverManagerRegistration { termination_state_proxy, dir } = match self
            .mutable_inner
            .borrow_mut()
            .registration_receiver
            .next()
            .on_timeout(timeout_time, || None)
            .await
        {
            Some(registration) => Ok(registration),
            None => Err(format_err!("registration_receiver channel closed")),
        }?;

        self.inspect.registration_time.set(get_current_timestamp().0);

        // Connect our `local_devfs_server` to the received DevFs channel from Driver Manager
        let local_devfs_server = self
            .mutable_inner
            .borrow_mut()
            .local_devfs_server
            .take()
            .ok_or(format_err!("Missing local_devfs_server"))
            .or_debug_panic()?;
        dir.clone(fio::OpenFlags::CLONE_SAME_RIGHTS, local_devfs_server.into_channel().into())?;

        // Clone `termination_state_proxy` and retrieve the closed handler function in preparation
        // to set up the channel closed handler task
        let termination_state_proxy_clone = termination_state_proxy.clone();
        let proxy_close_handler = self
            .mutable_inner
            .borrow_mut()
            .termination_channel_closed_handler
            .take()
            .ok_or(format_err!("Missing termination_channel_closed_handler"))
            .or_debug_panic()?;

        // Create the channel closed handler task
        let monitor_termination_channel_closed_task = fasync::Task::local(async move {
            let _ = termination_state_proxy_clone.on_closed().await;
            proxy_close_handler();
        });

        let mut inner_mut = self.mutable_inner.borrow_mut();
        inner_mut.monitor_termination_channel_closed_task =
            Some(monitor_termination_channel_closed_task);
        inner_mut.termination_state_proxy = Some(termination_state_proxy);

        self.init_done.signal();

        Ok(())
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, PowerManagerError> {
        match msg {
            Message::SetTerminationSystemState(state) => {
                self.handle_set_termination_state_message(*state).await
            }
            _ => Err(PowerManagerError::Unsupported),
        }
    }
}

struct InspectData {
    // Nodes
    _root_node: inspect::Node,

    // Properties
    registration_timeout_config: inspect::UintProperty,
    registration_time: inspect::IntProperty,
    termination_state: inspect::StringProperty,
    set_termination_errors: RefCell<BoundedListNode>,
}

impl InspectData {
    const NUM_SET_TERMINATION_ERRORS: usize = 10;

    fn new(parent: &inspect::Node, name: String) -> Self {
        let root_node = parent.create_child(name);

        Self {
            registration_timeout_config: root_node
                .create_uint("registration_timeout_config (s)", 0),
            registration_time: root_node.create_int("registration_time", 0),
            termination_state: root_node.create_string("termination_state", "None"),
            set_termination_errors: RefCell::new(BoundedListNode::new(
                root_node.create_child("set_termination_errors"),
                Self::NUM_SET_TERMINATION_ERRORS,
            )),
            _root_node: root_node,
        }
    }

    fn log_set_termination_error(&self, state: String, error: String) {
        inspect_log!(self.set_termination_errors.borrow_mut(), state: state, error: error)
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use futures::future::FusedFuture;
    use futures::{join, poll, select};
    use inspect::assert_data_tree;
    use std::cell::Cell;

    /// Creates a fake implementation of the SystemStateTransition protocol. Responds to the
    /// SetTerminationSystemState request by calling the provided closure. The closure's return
    /// value is returned to the client.
    fn setup_fake_termination_state_service<T>(
        mut set_termination_state: T,
    ) -> fdevicemgr::SystemStateTransitionProxy
    where
        T: FnMut(fpowerstatecontrol::SystemPowerState) -> Result<(), zx::Status> + 'static,
    {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fdevicemgr::SystemStateTransitionMarker>()
                .unwrap();
        fasync::Task::local(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fdevicemgr::SystemStateTransitionRequest::SetTerminationSystemState {
                        state,
                        responder,
                    }) => {
                        let _ = responder
                            .send(&mut set_termination_state(state).map_err(|e| e.into_raw()));
                    }
                    e => panic!("Unexpected request: {:?}", e),
                }
            }
        })
        .detach();

        proxy
    }

    /// Tests that well-formed configuration JSON does not panic the `new_from_json` function.
    #[fasync::run_singlethreaded(test)]
    async fn test_new_from_json() {
        // With a registration_timeout
        let _ = DriverManagerHandlerBuilder::new_from_json(
            json::json!({
                "type": "DriverManagerHandler",
                "name": "driver_manager_handler",
                "config": {
                    "registration_timeout": 60.0
                }
            }),
            &HashMap::new(),
            &mut ServiceFs::new_local(),
        );

        // Without a registration_timeout
        let _ = DriverManagerHandlerBuilder::new_from_json(
            json::json!({
                "type": "DriverManagerHandler",
                "name": "driver_manager_handler",
                "config": {
                }
            }),
            &HashMap::new(),
            &mut ServiceFs::new_local(),
        );
    }

    /// Tests for the presence and correctness of Inspect data
    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_data() {
        let inspector = inspect::Inspector::new();

        // For this test, let the server succeed for the Reboot state but give an
        // error for any other state (so that we can test error paths)
        let termination_state_proxy = setup_fake_termination_state_service(|state| match state {
            fpowerstatecontrol::SystemPowerState::Reboot => Ok(()),
            _ => Err(zx::Status::INVALID_ARGS),
        });

        let node = DriverManagerHandlerBuilder::new()
            .inspect_root(inspector.root())
            .registration_timeout(Seconds(60.0))
            .driver_manager_registration(DriverManagerRegistration {
                termination_state_proxy,
                dir: fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap().0,
            })
            .build_and_init()
            .await;

        // Should succeed so `termination_state` will be populated
        let _ = node
            .handle_set_termination_state_message(fpowerstatecontrol::SystemPowerState::Reboot)
            .await;

        // Should fail so `set_termination_errors` will be populated
        let _ = node
            .handle_set_termination_state_message(fpowerstatecontrol::SystemPowerState::FullyOn)
            .await;

        assert_data_tree!(
            inspector,
            root: {
                DriverManagerHandler: {
                    "registration_timeout_config (s)": 60u64,
                    "registration_time": inspect::testing::AnyProperty,
                    "termination_state": "Reboot",
                    "set_termination_errors": {
                        "0": {
                            "state": "FullyOn",
                            "error": "InvalidArgument(\"Invalid state argument: FullyOn\")",
                            "@time": inspect::testing::AnyProperty
                        }
                    }
                }
            }
        );
    }

    /// Tests that we can connect to the node's `DriverManagerRegistration` service and successfully
    /// register with it.
    #[fasync::run_singlethreaded(test)]
    async fn test_wait_for_registration_success() {
        let mut service_fs = ServiceFs::new_local();
        let connector = service_fs.create_protocol_connector().unwrap();
        let node = DriverManagerHandlerBuilder::new()
            .outgoing_svc_dir(service_fs.root_dir())
            .build()
            .unwrap();

        fasync::Task::local(service_fs.collect()).detach();

        // Create valid registration parameters
        let (transition_client, _) =
            fidl::endpoints::create_endpoints::<fdevicemgr::SystemStateTransitionMarker>().unwrap();
        let (dir_client, _dir_server) =
            fidl::endpoints::create_endpoints::<fio::DirectoryMarker>().unwrap();

        // Connect to the node's `DriverManagerRegistration` service
        let registration_client = connector
            .connect_to_protocol::<fpowermanager::DriverManagerRegistrationMarker>()
            .unwrap();

        // Run the node's `init()` future and the FIDL client's `register()` request future together
        let (init_result, register_result) =
            join!(node.init(), registration_client.register(transition_client, dir_client));

        assert_matches!(init_result, Ok(()));
        assert_matches!(register_result.unwrap(), Ok(()));
    }

    /// Tests that the node expectedly returns an error if the registration timeout expires.
    #[test]
    fn test_wait_for_registration_timeout() {
        let mut exec = fasync::TestExecutor::new_with_fake_time().unwrap();
        exec.set_fake_time(fasync::Time::from_nanos(0));

        let mut service_fs = ServiceFs::new_local();
        let node = DriverManagerHandlerBuilder::new()
            .registration_timeout(Seconds(1.0))
            .outgoing_svc_dir(service_fs.root_dir())
            .build()
            .unwrap();

        let mut init_future = node.init();

        // Try to run `init_future` so that the timer becomes active
        assert!(exec.run_until_stalled(&mut init_future).is_pending());

        // Force the timeout to expire and verify it would've fired as the expected time
        assert_eq!(exec.wake_next_timer(), Some(fasync::Time::from_nanos(1e9 as i64)));

        // Verify the `init()` call returns an error
        assert_matches!(
            exec.run_until_stalled(&mut init_future),
            futures::task::Poll::Ready(Err(_))
        );
    }

    /// Tests that sending a register request with invalid handles results in an error returned to
    /// the client and DriverManagerHandler still waiting in `init()`.
    #[fasync::run_singlethreaded(test)]
    async fn test_registration_invalid_handles() {
        let mut service_fs = ServiceFs::new_local();
        let connector = service_fs.create_protocol_connector().unwrap();
        let node = DriverManagerHandlerBuilder::new()
            .outgoing_svc_dir(service_fs.root_dir())
            .build()
            .unwrap();

        fasync::Task::local(service_fs.collect()).detach();

        // Create invalid registration parameters
        let transition_client =
            fidl::endpoints::create_endpoints::<fdevicemgr::SystemStateTransitionMarker>()
                .unwrap()
                .0;
        let dir_client =
            fidl::endpoints::ClientEnd::<fio::DirectoryMarker>::from(zx::Handle::invalid());

        // Connect to the node's `DriverManagerRegistration` service
        let registration_client = connector
            .connect_to_protocol::<fpowermanager::DriverManagerRegistrationMarker>()
            .unwrap();

        let mut node_init_future = node.init().fuse();
        let mut register_future =
            registration_client.register(transition_client, dir_client).fuse();

        // Run the node's `init()` future and the FIDL client's `register()` request future
        // together. Only `register()` is expected to complete (with an error).
        select! {
            _init_result = node_init_future => panic!("init completed unexpectedly"),
            register_result = register_future => {
                assert!(register_result.is_err())
            }
        }

        // Verify init() is still pending
        assert!(!node_init_future.is_terminated());
    }

    /// Tests that the proxy closure monitor fires after the underlying channel is closed
    #[fasync::run_singlethreaded(test)]
    async fn test_termination_channel_closure() {
        // Channel to notify the test when the proxy closure handler has fired
        let (mut channel_closed_sender, mut channel_closed_receiver) = mpsc::channel(1);

        // Create the registration parameters
        let (dir_client, _dir_server) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let (termination_state_proxy, termination_state_server) =
            fidl::endpoints::create_proxy::<fdevicemgr::SystemStateTransitionMarker>().unwrap();
        let registration = DriverManagerRegistration { termination_state_proxy, dir: dir_client };

        let _node = DriverManagerHandlerBuilder::new()
            .driver_manager_registration(registration)
            .termination_channel_closed_handler(Box::new(move || {
                channel_closed_sender.try_send(()).unwrap()
            }))
            .build_and_init()
            .await;

        // Drop the server end to close the channel
        drop(termination_state_server);

        // An item received on `channel_closed_receiver` indicates the proxy closure handler has
        // fired
        assert_matches!(channel_closed_receiver.next().await, Some(()));
    }

    /// Tests that DriverManagerHandler correctly binds the DevFs channel received from Driver
    /// Manager, allowing connections to drivers.
    #[fasync::run_singlethreaded(test)]
    async fn test_connect_to_driver() {
        use vfs::{directory::entry::DirectoryEntry, pseudo_directory};

        let fake_driver_path = "/dev/class/fake".to_string();

        // Create a directory proxy connected to a fake devfs containing a driver at "class/fake".
        //
        // This fake driver was chosen to implement fuchsia.device.manager.SystemStateTransition and
        // responds to SetTerminationSystemState requests. This protocol was chosen simply because
        // the code already has a dependency on it and it can be easily used to verify the FIDL
        // channel is set up properly.
        let devfs_proxy = {
            let fake_devfs = pseudo_directory! {
                "class" => pseudo_directory! {
                    "fake" => vfs::service::host(move |mut stream: fdevicemgr::SystemStateTransitionRequestStream| {
                        async move {
                            match stream.try_next().await.unwrap() {
                                Some(fdevicemgr::SystemStateTransitionRequest::SetTerminationSystemState {
                                    state: _, responder
                                }) => {
                                    let _ = responder.send(&mut Ok(()));
                                }
                                e => panic!("Unexpected request: {:?}", e),
                            }
                        }
                    })
                }
            };

            let (devfs_proxy, devfs_server) = fidl::endpoints::create_proxy().unwrap();
            fake_devfs.open(
                vfs::execution_scope::ExecutionScope::new(),
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                fio::MODE_TYPE_DIRECTORY,
                vfs::path::Path::dot(),
                devfs_server,
            );

            devfs_proxy
        };

        // Pass the directory proxy to the fake devfs to the node under test via its registration
        let registration = DriverManagerRegistration {
            termination_state_proxy: setup_fake_termination_state_service(|_| Ok(())),
            dir: fio::DirectoryProxy::from_channel(devfs_proxy.into_channel().unwrap()),
        };

        // Creating the node causes a placeholder "/dev" to be created
        let node = DriverManagerHandlerBuilder::new()
            .driver_manager_registration(registration)
            .build()
            .unwrap();

        // A future to connect to the fake driver
        let mut connect_driver_future = Box::pin(crate::utils::connect_to_driver::<
            fdevicemgr::SystemStateTransitionMarker,
        >(&fake_driver_path));

        // Verify the future initially hangs
        assert!(poll!(&mut connect_driver_future).is_pending());

        // After init completes, the fake devfs will be accessible
        node.init().await.unwrap();

        // The future should now complete, returning a proxy to the fake driver
        let fake_driver_proxy = connect_driver_future.await.unwrap();

        // Verify we can make a FIDL call to the fake driver
        fake_driver_proxy
            .set_termination_system_state(fpowerstatecontrol::SystemPowerState::Reboot)
            .await
            .expect("set_termination_system_state FIDL failed")
            .expect("set_termination_system_state returned error");
    }

    /// Tests that the DriverManagerHandler correctly processes the SetTerminationState message by
    /// calling out to the Driver Manager using the termination state proxy.
    #[fasync::run_singlethreaded(test)]
    async fn test_set_termination_state() {
        let termination_state = Rc::new(Cell::new(fpowerstatecontrol::SystemPowerState::FullyOn));
        let termination_state_clone = termination_state.clone();

        let (dir_client, _dir_server) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let registration = DriverManagerRegistration {
            termination_state_proxy: setup_fake_termination_state_service(move |state| {
                termination_state_clone.set(state);
                Ok(())
            }),
            dir: dir_client,
        };

        let node = DriverManagerHandlerBuilder::new()
            .driver_manager_registration(registration)
            .build_and_init()
            .await;

        // Send the message and verify it returns successfully
        assert_matches!(
            node.handle_message(&Message::SetTerminationSystemState(
                fpowerstatecontrol::SystemPowerState::Reboot
            ))
            .await,
            Ok(MessageReturn::SetTerminationSystemState)
        );

        // Verify the fake termination state service received the correct request
        assert_eq!(termination_state.get(), fpowerstatecontrol::SystemPowerState::Reboot);
    }

    /// Tests for correct ordering of nodes within each available node config file. The
    /// test verifies that if the DriverManagerHandler node is present in the config file, then it
    /// is listed before any other nodes that require a driver connection (identified as a node that
    /// contains a string config key called "driver_path").
    #[test]
    pub fn test_config_files() -> Result<(), anyhow::Error> {
        crate::utils::test_each_node_config_file(|config_file| {
            let driver_manager_handler_index =
                config_file.iter().position(|config| config["type"] == "DriverManagerHandler");
            let first_node_using_drivers_index =
                config_file.iter().position(|config| config["config"].get("driver_path").is_some());

            if driver_manager_handler_index.is_some()
                && first_node_using_drivers_index.is_some()
                && driver_manager_handler_index.unwrap() > first_node_using_drivers_index.unwrap()
            {
                return Err(format_err!(
                    "Must list DriverManagerHandler node before {}",
                    config_file[first_node_using_drivers_index.unwrap()]["name"]
                ));
            }

            Ok(())
        })
    }

    /// Tests that messages sent to the node are asynchronously blocked until the node's `init()`
    /// has completed.
    #[fasync::run_singlethreaded(test)]
    async fn test_require_init() {
        let (dir_client, _dir_server) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let registration = DriverManagerRegistration {
            termination_state_proxy: setup_fake_termination_state_service(|_| Ok(())),
            dir: dir_client,
        };

        // Create the node without `init()`
        let node = DriverManagerHandlerBuilder::new()
            .driver_manager_registration(registration)
            .build()
            .unwrap();

        // Future to send the node a message
        let mut message_future = node.handle_message(&Message::SetTerminationSystemState(
            fpowerstatecontrol::SystemPowerState::Reboot,
        ));

        assert!(poll!(&mut message_future).is_pending());
        assert_matches!(node.init().await, Ok(()));
        assert_matches!(message_future.await, Ok(MessageReturn::SetTerminationSystemState));
    }
}
