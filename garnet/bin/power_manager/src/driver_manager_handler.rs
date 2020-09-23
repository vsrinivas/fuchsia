// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::PowerManagerError;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::system_shutdown_handler;
use crate::types::Seconds;
use crate::utils::get_current_timestamp;
use anyhow::{format_err, Context, Error};
use async_trait::async_trait;
use fidl_fuchsia_device_manager as fdevicemgr;
use fidl_fuchsia_hardware_power_statecontrol as fpowerstatecontrol;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_power_manager as fpowermanager;
use fuchsia_async::{self as fasync, DurationExt, TimeoutExt};
use fuchsia_component::server::{ServiceFs, ServiceObjLocal};
use fuchsia_inspect::{self as inspect, Property};
use fuchsia_inspect_contrib::{inspect_log, nodes::BoundedListNode};
use fuchsia_zircon::{self as zx, AsHandleRef};
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
    termination_channel_closed_handler: Box<dyn FnOnce() + 'static>,
    service_fs: Option<&'a mut ServiceFs<ServiceObjLocal<'b, ()>>>,
    inspect_root: Option<&'a inspect::Node>,
}

impl<'a, 'b> DriverManagerHandlerBuilder<'a, 'b> {
    pub fn new() -> Self {
        Self {
            registration_timeout: None,
            driver_manager_registration: None,
            termination_channel_closed_handler: Box::new(termination_channel_closed_handler),
            service_fs: None,
            inspect_root: None,
        }
    }

    pub fn new_from_json(
        json_data: json::Value,
        _nodes: &HashMap<String, Rc<dyn Node>>,
        service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
    ) -> Self {
        #[derive(Deserialize)]
        struct Config {
            registration_timeout_s: Option<f64>,
        };

        #[derive(Deserialize)]
        struct JsonData {
            config: Config,
        };

        let data: JsonData = json::from_value(json_data).unwrap();
        let mut builder = Self::new().with_service_fs(service_fs);

        if let Some(timeout) = data.config.registration_timeout_s {
            builder = builder.with_registration_timeout(Seconds(timeout));
        }

        builder
    }

    pub fn with_service_fs(
        mut self,
        service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
    ) -> Self {
        self.service_fs = Some(service_fs);
        self
    }

    pub fn with_registration_timeout(mut self, timeout: Seconds) -> Self {
        self.registration_timeout = Some(timeout);
        self
    }

    #[cfg(test)]
    pub fn with_termination_channel_closed_handler(
        mut self,
        handler: Box<impl FnOnce() + 'static>,
    ) -> Self {
        self.termination_channel_closed_handler = handler;
        self
    }

    #[cfg(test)]
    pub fn with_driver_manager_registration(
        mut self,
        registration: DriverManagerRegistration,
    ) -> Self {
        self.driver_manager_registration = Some(registration);
        self
    }

    #[cfg(test)]
    pub fn with_inspect_root(mut self, root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(root);
        self
    }

    /// Constructs the DriverManagerHandler node. In order to construct a fully initialized node
    /// that is able to serve driver connections for other nodes within the Power Manager, this
    /// `build` function will block until it receives the required registration from the Driver
    /// Manager. The function returns an error if the DriverManagerHandlerBuilder was provided with
    /// a timeout value and that timeout expires before receiving the registration.
    pub async fn build(self) -> Result<Rc<DriverManagerHandler>, Error> {
        // Optionally use the default inspect root node
        let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());

        // If a DriverManagerRegistration was provided, then use it. Otherwise, call the function
        // that waits for the Driver Manager to provide us with the registration. If the
        // registration was not received within the specified timeout, an error is returned.
        let registration = if self.driver_manager_registration.is_some() {
            self.driver_manager_registration.unwrap()
        } else {
            await_registration(
                self.registration_timeout,
                self.service_fs.ok_or(format_err!("ServiceFs required"))?,
            )
            .await?
        };

        // Set up Inspect and log the registration timeout configuration
        let inspect = InspectData::new(inspect_root, "DriverManagerHandler".to_string());
        inspect
            .registration_timeout_config
            .set(self.registration_timeout.unwrap_or(Seconds(0.0)).0 as u64);
        inspect.registration_time.set(get_current_timestamp().0);

        // Set up a signal handler to monitor `termination_state_proxy` for a PEER_CLOSED signal. By
        // default, if the channel is closed then the system will be forcefully shutdown.
        enable_proxy_close_handler(
            registration.termination_state_proxy.clone(),
            self.termination_channel_closed_handler,
        );

        // Bind the received Directory channel to the namespace. This lets the received drivers be
        // accessed in the usual way (using `fdio::service_connect` and a /dev path).
        bind_driver_directory(registration.dir).context("Failed to bind driver directory")?;

        let node = Rc::new(DriverManagerHandler {
            termination_state_proxy: registration.termination_state_proxy,
            inspect,
        });

        Ok(node)
    }
}

/// Default handler function that will be called if the fuchsia.device.manager.SystemStateTransition
/// protocol instance that was provided during registration is ever closed.
fn termination_channel_closed_handler() {
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

/// Waits to receive a successful registration from the Driver Manager. The function returns after a
/// registration was successfully received or after the timeout expires, if one was provided.
async fn await_registration<'a, 'b>(
    timeout: Option<Seconds>,
    service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
) -> Result<DriverManagerRegistration, Error> {
    fuchsia_trace::duration!(
        "power_manager",
        "DriverManagerHandler::await_registration",
        "timeout" => timeout.unwrap_or(Seconds(0.0)).0
    );

    // Publish the fuchsia.power.manager.DriverManagerRegistration service and get the channel that
    // the service will use to send the registration
    let registration_receiver = setup_registration_service(service_fs);

    // Wait to receive the registration on the receiver channel, or for the timeout to expire
    let result = wait_for_registration(timeout, registration_receiver, service_fs).await;
    fuchsia_trace::instant!(
        "power_manager",
        "DriverManagerHandler::await_registration_result",
        fuchsia_trace::Scope::Thread,
        "result" => format!("{:?}", result).as_str()
    );

    result
}

/// Publishes the fuchsia.power.manager.DriverManagerRegistration service so that the Driver manager
/// can send the registration that the Power Manager requires. The service is provided with the
/// sender end of a mpsc channel so that it can send the registration back once it is received. The
/// function returns the receiver end of the channel.
fn setup_registration_service<'a, 'b>(
    service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
) -> mpsc::Receiver<DriverManagerRegistration> {
    let (registration_sender, registration_receiver) = mpsc::channel(1);

    // Publish the fuchsia.power.manager.DriverManagerRegistration service
    service_fs.dir("svc").add_fidl_service(
        move |stream: fpowermanager::DriverManagerRegistrationRequestStream| {
            handle_new_driver_manager_registration_stream(stream, registration_sender.clone());
        },
    );

    registration_receiver
}

/// Waits to receive registration over the mpsc channel receiver end. If a timeout value is
/// provided, then the function will return an error after the timeout expires. The provided
/// `service_fs` is polled to allow the published fuchsia.power.manager.DriverManagerRegistration
/// service to run
async fn wait_for_registration(
    timeout: Option<Seconds>,
    mut registration_receiver: mpsc::Receiver<DriverManagerRegistration>,
    mut service_fs: impl Stream<Item = ()> + std::marker::Unpin,
) -> Result<DriverManagerRegistration, Error> {
    // Use the provided `timeout` and the current time to calculate a deadline
    let timeout_time = if timeout.is_some() {
        zx::Duration::from_seconds(timeout.unwrap().0 as i64).after_now()
    } else {
        fasync::Time::INFINITE
    };

    // We must poll the provided `service_fs` so that the published
    // fuchsia.power.manager.DriverManagerRegistration service can run. If the stream emits an item,
    // just continue in this loop until we get a registration on the channel or we hit the timeout.
    loop {
        match future::select(
            registration_receiver.next().on_timeout(timeout_time, || None),
            service_fs.next(),
        )
        .await
        {
            future::Either::Left((Some(registration), _)) => return Ok(registration),
            future::Either::Left((None, _)) => {
                return Err(format_err!("Timed out waiting for registration"))
            }
            future::Either::Right(_) => {}
        }
    }
}

/// Handles a new connection to the fuchsia.power.manager.DriverManagerRegistration service. The
/// provided `stream` contains the requests sent to this connection. The provided
/// `registration_sender` is used to notify the receipt of the registration.
fn handle_new_driver_manager_registration_stream(
    mut stream: fpowermanager::DriverManagerRegistrationRequestStream,
    mut registration_sender: mpsc::Sender<DriverManagerRegistration>,
) {
    fuchsia_trace::instant!(
        "power_manager",
        "DriverManagerHandler::handle_new_driver_manager_registration_stream",
        fuchsia_trace::Scope::Thread
    );
    fasync::Task::local(
        async move {
            while let Some(req) = stream.try_next().await? {
                match req {
                    fpowermanager::DriverManagerRegistrationRequest::Register {
                        system_state_transition,
                        dir,
                        responder,
                    } => {
                        fuchsia_trace::instant!(
                            "power_manager",
                            "DriverManagerHandler::register_request",
                            fuchsia_trace::Scope::Thread
                        );

                        let mut result = handle_driver_manager_registration(
                            system_state_transition,
                            dir,
                            &mut registration_sender,
                        );
                        let _ = responder.send(&mut result);

                        fuchsia_trace::instant!(
                            "power_manager",
                            "DriverManagerHandler::register_request_result",
                            fuchsia_trace::Scope::Thread,
                            "result" => format!("{:?}", result).as_str()
                        );

                        // After we successfully receive the registration, we can stop processing
                        // any requests on this service connection
                        if result.is_ok() {
                            break;
                        } else {
                            error!("Received invalid registration");
                        }
                    }
                }
            }
            Ok(())
        }
        .unwrap_or_else(|e: anyhow::Error| error!("{:?}", e)),
    )
    .detach();
}

/// Handles a register request sent to the fuchsia.power.manager.DriverManagerRegistration service.
/// The function determines if the registration is valid and then sends it over the mpsc channel
/// sender end.
fn handle_driver_manager_registration(
    termination_state_proxy: fidl::endpoints::ClientEnd<fdevicemgr::SystemStateTransitionMarker>,
    dir: fidl::endpoints::ClientEnd<fio::DirectoryMarker>,
    registration_sender: &mut mpsc::Sender<DriverManagerRegistration>,
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
        .map_err(|_| fpowermanager::RegistrationError::Internal)?;
    Ok(())
}

/// Spawns a Future that monitors the given `proxy` for a CHANNEL_PEER_CLOSED signal. If the signal
/// is present, then the `handler` function is called.
fn enable_proxy_close_handler(
    proxy: fdevicemgr::SystemStateTransitionProxy,
    handler: impl FnOnce() + 'static,
) {
    fasync::Task::local(async move {
        let _ =
            fasync::OnSignals::new(&proxy.as_handle_ref(), zx::Signals::CHANNEL_PEER_CLOSED).await;
        handler();
    })
    .detach();
}

/// Creates a "/dev" directory within the namespace that is bound to the provided DirectoryProxy.
fn bind_driver_directory(dir: fio::DirectoryProxy) -> Result<(), Error> {
    fdio::Namespace::installed()?
        .bind(
            "/dev",
            dir.into_channel()
                .map_err(|_| format_err!("Failed to convert DirectoryProxy into channel"))?
                .into_zx_channel(),
        )
        .map_err(|e| e.into())
}

pub struct DriverManagerHandler {
    /// Protocol instance that the Power Manager uses to set the Driver Manager's termination system
    /// state.
    termination_state_proxy: fdevicemgr::SystemStateTransitionProxy,

    /// Struct for managing Component Inspection data
    inspect: InspectData,
}

impl DriverManagerHandler {
    /// Handle the SetTerminationState message. The function uses `self.termination_state_proxy` to
    /// set the Driver Manager's termination state.
    async fn handle_set_termination_state_message(
        &self,
        state: fpowerstatecontrol::SystemPowerState,
    ) -> Result<MessageReturn, PowerManagerError> {
        // TODO(fxbug.dev/44484): This string must live for the duration of the function because the trace
        // macro uses it when the function goes out of scope. Therefore, it must be bound here and
        // not used anonymously at the macro callsite.
        let state_str = format!("{:?}", state);
        fuchsia_trace::duration!(
            "power_manager",
            "DriverManagerHandler::handle_set_termination_state_message",
            "state" => state_str.as_str()
        );

        let result = self
            .termination_state_proxy
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

#[async_trait(?Send)]
impl Node for DriverManagerHandler {
    fn name(&self) -> String {
        "DriverManagerHandler".to_string()
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
mod tests {
    use super::*;
    use crate::utils::connect_proxy;
    use inspect::assert_inspect_tree;
    use matches::assert_matches;
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
            .with_inspect_root(inspector.root())
            .with_registration_timeout(Seconds(60.0))
            .with_driver_manager_registration(DriverManagerRegistration {
                termination_state_proxy,
                dir: fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap().0,
            })
            .build()
            .await
            .unwrap();

        // Should succeed so `termination_state` will be populated
        let _ = node
            .handle_set_termination_state_message(fpowerstatecontrol::SystemPowerState::Reboot)
            .await;

        // Should fail so `set_termination_errors` will be populated
        let _ = node
            .handle_set_termination_state_message(fpowerstatecontrol::SystemPowerState::FullyOn)
            .await;

        assert_inspect_tree!(
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

    /// Tests that the `handle_new_driver_manager_registration_stream` (service handler) and
    /// `wait_for_registration` functions will work as intended by:
    ///     1. the server receives the register request and returns success to the client
    ///     2. the server places the received registration into the mpsc sender end
    ///     3. the registration is received on the mpsc receiver end
    #[fasync::run_singlethreaded(test)]
    async fn test_wait_for_registration_success() {
        // Immitate opening a connection with the DriverManagerRegistration service
        let (sender, receiver) = mpsc::channel(1);
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<
            fpowermanager::DriverManagerRegistrationMarker,
        >()
        .unwrap();
        handle_new_driver_manager_registration_stream(stream, sender);

        // Send the register request and verify it is successful
        let (transition_client, _) =
            fidl::endpoints::create_endpoints::<fdevicemgr::SystemStateTransitionMarker>().unwrap();
        let (dir_client, _) = fidl::endpoints::create_endpoints::<fio::DirectoryMarker>().unwrap();
        assert!(proxy.register(transition_client, dir_client).await.unwrap().is_ok());

        // Verify the registration was received
        assert!(wait_for_registration(None, receiver, futures::stream::pending()).await.is_ok());
    }

    /// Tests that the timeout functionality of the `wait_for_registration` works as expected by
    /// returning an error after the registration is not received after the expected time.
    #[test]
    fn test_wait_for_registration_timeout() {
        let mut exec = fasync::Executor::new_with_fake_time().unwrap();
        exec.set_fake_time(fasync::Time::from_nanos(0));

        let (_sender, receiver) = mpsc::channel(1);
        let mut wait_future =
            wait_for_registration(Some(Seconds(1.0)), receiver, futures::stream::pending()).boxed();

        // Try to run the wait_future so that the timer becomes active
        assert!(exec.run_until_stalled(&mut wait_future).is_pending());

        // Force the timeout to expire and verify it would've fired as the expected time
        assert_eq!(exec.wake_next_timer(), Some(fasync::Time::from_nanos(1e9 as i64)));

        // Verify the `wait_for_registration` call returns an error
        assert_matches!(
            exec.run_until_stalled(&mut wait_future),
            futures::task::Poll::Ready(Err(_))
        );
    }

    /// Tests that sending a register request with invalid handles results in an error returned to
    /// the client.
    #[test]
    fn test_registration_invalid_handles() {
        let mut exec = fasync::Executor::new().unwrap();

        // Immitate opening a connection with the DriverManagerRegistration service
        let (sender, receiver) = mpsc::channel(1);
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<
            fpowermanager::DriverManagerRegistrationMarker,
        >()
        .unwrap();
        handle_new_driver_manager_registration_stream(stream, sender);

        // Set up the fake registration using an invalid directory handle
        let transition_client =
            fidl::endpoints::create_endpoints::<fdevicemgr::SystemStateTransitionMarker>()
                .unwrap()
                .0;
        let dir_client =
            fidl::endpoints::ClientEnd::<fio::DirectoryMarker>::from(zx::Handle::invalid());

        // Verify the register request returns an error
        assert!(exec.run_singlethreaded(proxy.register(transition_client, dir_client)).is_err());

        // Verify the `wait_for_registration` is still pending waiting for valid registration
        assert!(exec
            .run_until_stalled(
                &mut wait_for_registration(None, receiver, futures::stream::pending()).boxed()
            )
            .is_pending());
    }

    /// Tests that the proxy closure monitor fires after the underlying channel is closed
    #[test]
    fn test_termination_channel_closure() {
        let mut exec = fasync::Executor::new().unwrap();

        let channel_closed = Rc::new(Cell::new(false));
        let channel_closed_clone = channel_closed.clone();

        let (termination_state_proxy, termination_state_server) =
            fidl::endpoints::create_proxy::<fdevicemgr::SystemStateTransitionMarker>().unwrap();

        let registration = DriverManagerRegistration {
            termination_state_proxy,
            dir: fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap().0,
        };

        let _node = exec
            .run_singlethreaded(
                DriverManagerHandlerBuilder::new()
                    .with_driver_manager_registration(registration)
                    .with_termination_channel_closed_handler(Box::new(move || {
                        channel_closed_clone.set(true)
                    }))
                    .build(),
            )
            .unwrap();

        // Drop the server end to close the channel
        drop(termination_state_server);

        // Lets the pending monitor future run
        assert!(exec.run_until_stalled(&mut future::pending::<()>()).is_pending());

        // Verify the channel close handler was run
        assert_eq!(channel_closed.get(), true);
    }

    /// Tests the driver directory binding functionality by setting up the DriverManagerHandler node
    /// with a Directory channel that the node will then bind to the namespace. The test connects to
    /// a driver in the namespace at "/dev", which will be routed to the provided Directory channel.
    /// The connection is tested to be valid by verifying a message is able to successfully be
    /// passed through the channel.
    #[fasync::run_singlethreaded(test)]
    async fn test_connect_proxy() {
        let (dir_proxy, mut dir_stream) =
            fidl::endpoints::create_proxy_and_stream::<fio::DirectoryMarker>().unwrap();

        let registration = DriverManagerRegistration {
            termination_state_proxy: setup_fake_termination_state_service(|_| Ok(())),
            dir: dir_proxy,
        };

        let _node = DriverManagerHandlerBuilder::new()
            .with_driver_manager_registration(registration)
            .build()
            .await
            .unwrap();

        // Connect to the fake driver
        let proxy = connect_proxy::<fio::DirectoryMarker>(&"/dev/class/fake".to_string()).unwrap();

        // Verify the fake directory received the Open request, and capture the server end that is
        // meant to be bound to the driver
        let fake_driver = match dir_stream.try_next().await.unwrap() {
            Some(fio::DirectoryRequest::Open { object, .. }) => object.into_channel(),
            e => panic!("Unexpected request: {:?}", e),
        };

        // Write a message into the client end and verify the fake driver receives it
        let mut buf = zx::MessageBuf::new();
        assert!(proxy.write(b"Foo", &mut vec![]).is_ok());
        assert!(fake_driver.read(&mut buf).is_ok());
        assert_eq!(buf.bytes(), b"Foo");
    }

    /// Tests that the DriverManagerHandler correctly processes the SetTerminationState message by
    /// calling out to the Driver Manager using the termination state proxy.
    #[fasync::run_singlethreaded(test)]
    async fn test_set_termination_state() {
        let termination_state = Rc::new(Cell::new(fpowerstatecontrol::SystemPowerState::FullyOn));
        let termination_state_clone = termination_state.clone();

        let registration = DriverManagerRegistration {
            termination_state_proxy: setup_fake_termination_state_service(move |state| {
                termination_state_clone.set(state);
                Ok(())
            }),
            dir: fidl::endpoints::create_proxy_and_stream::<fio::DirectoryMarker>().unwrap().0,
        };

        let node = DriverManagerHandlerBuilder::new()
            .with_driver_manager_registration(registration)
            .build()
            .await
            .unwrap();

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
}
