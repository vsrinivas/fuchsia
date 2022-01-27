// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        error::PowerManagerError,
        message::{Message, MessageReturn},
        node::Node,
    },
    anyhow::Result,
    async_trait::async_trait,
    async_utils::hanging_get::server as hanging_get,
    fidl_fuchsia_power_profile::{self as fprofile, Profile},
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceObjLocal},
    fuchsia_inspect::{self as inspect, Property},
    futures::prelude::*,
    log::*,
    serde_json as json,
    std::{cell::RefCell, collections::HashMap, rc::Rc},
};

/// Node: SystemProfileHandler
///
/// Summary: Hosts the System Power Profile service. The service works by collecting input states
///   from various sources (initially from the Activity and InputSettings services) and making a
///   determination about the system power profile. Clients can register with the service to learn
///   about the current power profile and use hanging-gets to be notified about changes to the
///   profile.
///
/// Handles Messages:
///     - NotifyMicEnabledChanged
///     - NotifyUserActiveChanged
///
/// Sends Messages: N/A
///
/// FIDL dependencies:
///     - fuchsia.power.profile.Watcher: the node implements a service of this protocol to
///       communicate the power profile

/// A builder for constructing the SystemProfileHandler node.
#[derive(Default)]
pub struct SystemProfileHandlerBuilder<'a, 'b> {
    service_fs: Option<&'a mut ServiceFs<ServiceObjLocal<'b, ()>>>,
    inspect_root: Option<&'a inspect::Node>,
    hanging_get_broker: Option<ProfileHangingGet>,
}

impl<'a, 'b> SystemProfileHandlerBuilder<'a, 'b> {
    pub fn new_from_json(
        _json_data: json::Value,
        _nodes: &HashMap<String, Rc<dyn Node>>,
        service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
    ) -> Self {
        Self { service_fs: Some(service_fs), ..Default::default() }
    }

    #[cfg(test)]
    fn new() -> Self {
        SystemProfileHandlerBuilder::default()
    }

    #[cfg(test)]
    fn with_inspect_root(mut self, root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(root);
        self
    }

    #[cfg(test)]
    fn with_hanging_get_broker(mut self, hanging_get_broker: ProfileHangingGet) -> Self {
        self.hanging_get_broker = Some(hanging_get_broker);
        self
    }

    pub fn build(self) -> Result<Rc<SystemProfileHandler>> {
        // Allow test to override
        let inspect_root =
            self.inspect_root.unwrap_or_else(|| inspect::component::inspector().root());
        let inspect = InspectData::new(inspect_root, "SystemProfileHandler".to_string());

        let input_states = InputStates::new();
        let current_profile = profile_from_input_states(&input_states);

        // Initialize the Inspect properties
        inspect.set_inputs(&input_states);
        inspect.set_profile(current_profile);

        // Allow test to override
        let hanging_get_broker =
            self.hanging_get_broker.unwrap_or_else(|| create_hanging_get_broker(current_profile));

        let node = SystemProfileHandler {
            inner: RefCell::new(SystemProfileHandlerInner {
                current_profile,
                input_states,
                server_publisher: hanging_get_broker.new_publisher(),
            }),
            inspect,
        };

        // Publish the power profile service
        if let Some(service_fs) = self.service_fs {
            SystemProfileHandler::publish_profile_service(service_fs, hanging_get_broker);
        }

        Ok(Rc::new(node))
    }
}

/// Defines the concrete type of the `notify` function used by the HangingGet server.
type ProfileChangeFn = Box<dyn Fn(&Profile, fprofile::WatcherWatchResponder) -> bool>;

/// Defines the concrete type of the HangingGet server.
type ProfileHangingGet =
    hanging_get::HangingGet<Profile, fprofile::WatcherWatchResponder, ProfileChangeFn>;

/// Defines the concrete type of the HangingGet publisher.
type ProfilePublisher =
    hanging_get::Publisher<Profile, fprofile::WatcherWatchResponder, ProfileChangeFn>;

/// Defines the concrete type of the HangingGet subscriber.
type ProfileSubscriber =
    hanging_get::Subscriber<Profile, fprofile::WatcherWatchResponder, ProfileChangeFn>;

pub struct SystemProfileHandler {
    /// Inner state that requires mutability.
    inner: RefCell<SystemProfileHandlerInner>,
    inspect: InspectData,
}

/// Inner state for SystemProfileHandler that requires mutability.
struct SystemProfileHandlerInner {
    current_profile: Profile,
    input_states: InputStates,
    server_publisher: ProfilePublisher,
}

impl SystemProfileHandler {
    /// Publishes the fuchsia.power.profile.Watcher service. Each time a client connects to the
    /// service, a new `ProfileSubscriber` is created which manages the hanging-get connection.
    fn publish_profile_service<'a, 'b>(
        service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
        mut hanging_get_broker: ProfileHangingGet,
    ) {
        service_fs.dir("svc").add_fidl_service(move |stream| {
            Self::handle_new_service_connection(stream, hanging_get_broker.new_subscriber());
        });
    }

    /// Handles a new client connection to the fuchsia.power.profile.Watcher service. A new detached
    /// Task is spawned for each connection.
    fn handle_new_service_connection(
        mut stream: fprofile::WatcherRequestStream,
        subscriber: ProfileSubscriber,
    ) {
        fuchsia_trace::instant!(
            "power_manager",
            "SystemProfileHandler::handle_new_service_connection",
            fuchsia_trace::Scope::Thread
        );

        fasync::Task::local(
            async move {
                while let Some(req) = stream.try_next().await? {
                    match req {
                        fprofile::WatcherRequest::Watch { responder } => {
                            // Register the responder with our hanging-get logic (response may not
                            // be sent immediately)
                            subscriber.register(responder)?;
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| error!("{:?}", e)),
        )
        .detach();
    }

    fn handle_mic_enabled_changed(
        &self,
        enabled: bool,
    ) -> Result<MessageReturn, PowerManagerError> {
        self.inner.borrow_mut().input_states.mic_enabled = enabled;
        self.handle_inputs_changed();
        Ok(MessageReturn::NotifyMicEnabledChanged)
    }

    fn handle_user_active_changed(&self, active: bool) -> Result<MessageReturn, PowerManagerError> {
        self.inner.borrow_mut().input_states.user_active = active;
        self.handle_inputs_changed();
        Ok(MessageReturn::NotifyUserActiveChanged)
    }

    /// Handle a change to inputs that affect the power profile. Called each time any of the inputs
    /// changes.
    fn handle_inputs_changed(&self) {
        self.inspect.set_inputs(&self.inner.borrow().input_states);

        if let Some(new_profile) = self.determine_new_profile() {
            self.set_new_profile(new_profile);
        }
    }

    /// Determines the current power profile. Returns Some(new_profile) if the determined profile is
    /// different from `self.current_profile`. Returns None if the profile is unchanged.
    ///
    /// Initially, the new profile is determined solely using `self.input_states`. However, in the
    /// future we may factor in the current profile value, timers, or configuration as well.
    fn determine_new_profile(&self) -> Option<Profile> {
        let inner = self.inner.borrow();
        let new_profile = profile_from_input_states(&inner.input_states);
        if new_profile != inner.current_profile {
            Some(new_profile)
        } else {
            None
        }
    }

    /// Sets a new Profile value.
    ///
    /// Among other things, this method updates the cached `current_profile` and notifies the
    /// `server_publisher`.
    fn set_new_profile(&self, profile: Profile) {
        self.inspect.set_profile(profile);

        let mut inner = self.inner.borrow_mut();
        inner.current_profile = profile;
        inner.server_publisher.set(profile);
    }
}

/// Creates a hanging-get broker for dispensing publishers and subscribers.
fn create_hanging_get_broker(initial_profile: Profile) -> ProfileHangingGet {
    // Called by the HangingGet server each time `server.set()` is called. Sends the new profile
    // value to all connected clients.
    let notify_fn: ProfileChangeFn = Box::new(|profile, responder| {
        let _ = responder.send((*profile).into());
        true // indicates that the client was successfully updated with the new profile
    });
    hanging_get::HangingGet::new(initial_profile, notify_fn)
}

#[async_trait(?Send)]
impl Node for SystemProfileHandler {
    fn name(&self) -> String {
        "SystemProfileHandler".to_string()
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, PowerManagerError> {
        match msg {
            Message::NotifyMicEnabledChanged(enabled) => self.handle_mic_enabled_changed(*enabled),
            Message::NotifyUserActiveChanged(active) => self.handle_user_active_changed(*active),
            _ => Err(PowerManagerError::Unsupported),
        }
    }
}

/// Represents the values of the various input states used to determine the system power profile.
#[derive(Debug)]
struct InputStates {
    mic_enabled: bool,
    user_active: bool,
}

impl InputStates {
    fn new() -> Self {
        Self { mic_enabled: false, user_active: false }
    }
}

/// Construct a new Profile based on the provided InputStates.
fn profile_from_input_states(input_states: &InputStates) -> Profile {
    if input_states.user_active {
        Profile::UserActive
    } else if input_states.mic_enabled {
        Profile::BackgroundActive
    } else {
        Profile::Idle
    }
}

struct InspectData {
    input_states_property: inspect::StringProperty,
    profile_property: inspect::StringProperty,
}

impl InspectData {
    fn new(parent: &inspect::Node, name: String) -> Self {
        let root = parent.create_child(name);

        let input_states_property = root.create_string("input_states", "");
        let profile_property = root.create_string("power_profile", "");

        parent.record(root);

        Self { input_states_property, profile_property }
    }

    fn set_inputs(&self, input_states: &InputStates) {
        self.input_states_property.set(format!("{:?}", input_states).as_str())
    }

    fn set_profile(&self, profile: Profile) {
        self.profile_property.set(format!("{:?}", profile).as_str())
    }
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches, fuchsia_inspect::assert_data_tree};

    /// Tests for the presence and correctness of dynamically-added inspect data
    #[test]
    fn test_inspect_data() {
        let inspector = inspect::Inspector::new();

        let node =
            SystemProfileHandlerBuilder::new().with_inspect_root(inspector.root()).build().unwrap();

        // Inspect should be empty by default
        assert_data_tree!(
            inspector,
            root: {
                "SystemProfileHandler": {
                    "input_states": "InputStates { mic_enabled: false, user_active: false }",
                    "power_profile": "Idle",
                }
            }
        );

        node.handle_mic_enabled_changed(true).unwrap();
        assert_data_tree!(
            inspector,
            root: {
                "SystemProfileHandler": {
                    "input_states": "InputStates { mic_enabled: true, user_active: false }",
                    "power_profile": "BackgroundActive"
                }
            }
        );

        node.handle_user_active_changed(true).unwrap();
        assert_data_tree!(
            inspector,
            root: {
                "SystemProfileHandler": {
                    "input_states": "InputStates { mic_enabled: true, user_active: true }",
                    "power_profile": "UserActive"
                }
            }
        );

        node.handle_mic_enabled_changed(false).unwrap();
        node.handle_user_active_changed(false).unwrap();
        assert_data_tree!(
            inspector,
            root: {
                "SystemProfileHandler": {
                    "input_states": "InputStates { mic_enabled: false, user_active: false }",
                    "power_profile": "Idle"
                }
            }
        );
    }

    /// Tests that SystemProfileHandler receives messages to update input states and correctly
    /// updates its Profile value.
    #[fasync::run_singlethreaded(test)]
    async fn test_profile_updates() {
        let node = SystemProfileHandlerBuilder::new().build().unwrap();

        // Idle initially
        assert_eq!(node.inner.borrow().current_profile, Profile::Idle);

        // Inject NotifyMicEnabled(true), so expect BackgroundActive
        assert_matches!(
            node.handle_message(&Message::NotifyMicEnabledChanged(true)).await,
            Ok(MessageReturn::NotifyMicEnabledChanged)
        );
        assert_eq!(node.inner.borrow().current_profile, Profile::BackgroundActive);

        // Inject UserActive(true), so expect UserActive
        assert_matches!(
            node.handle_message(&Message::NotifyUserActiveChanged(true)).await,
            Ok(MessageReturn::NotifyUserActiveChanged)
        );
        assert_eq!(node.inner.borrow().current_profile, Profile::UserActive);

        // Inject NotifyMicEnabled(false), so expect no profile update
        assert_matches!(
            node.handle_message(&Message::NotifyMicEnabledChanged(false)).await,
            Ok(MessageReturn::NotifyMicEnabledChanged)
        );
        assert_eq!(node.inner.borrow().current_profile, Profile::UserActive);

        // Inject UserActive(false), so expect Idle
        assert_matches!(
            node.handle_message(&Message::NotifyUserActiveChanged(false)).await,
            Ok(MessageReturn::NotifyUserActiveChanged)
        );
        assert_eq!(node.inner.borrow().current_profile, Profile::Idle);
    }

    /// Tests that profile changes are communicated via the server.
    #[test]
    fn test_profile_server() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let mut hanging_get_broker = create_hanging_get_broker(Profile::Idle);

        // Set up the service handler
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<fprofile::WatcherMarker>()
            .expect("Failed to create proxy and stream");
        SystemProfileHandler::handle_new_service_connection(
            stream,
            hanging_get_broker.new_subscriber(),
        );

        let node = SystemProfileHandlerBuilder::new()
            .with_hanging_get_broker(hanging_get_broker)
            .build()
            .expect("Failed to build node");

        // The first `watch` request should return immediately with Idle
        assert_matches!(
            exec.run_singlethreaded(proxy.watch()).expect("watch call failed"),
            fprofile::Profile::Idle
        );

        // Set mic_enabled to true and verify the client's next request returns BackgroundActive
        node.handle_mic_enabled_changed(true).unwrap();
        assert_matches!(
            exec.run_singlethreaded(proxy.watch()).expect("watch call failed"),
            fprofile::Profile::BackgroundActive
        );

        // Set user_active to true and verify the client's next request returns UserActive
        node.handle_user_active_changed(true).unwrap();
        assert_matches!(
            exec.run_singlethreaded(proxy.watch()).expect("watch call failed"),
            fprofile::Profile::UserActive
        );

        // Verify a new client request doesn't immediately return if the profile hasn't changed
        let mut watch_request = proxy.watch();
        assert!(exec.run_until_stalled(&mut watch_request).is_pending());

        // Verify once the profile does change, the correct profile is returned
        node.handle_user_active_changed(false).unwrap();
        assert_matches!(
            exec.run_singlethreaded(watch_request).expect("watch call failed"),
            fprofile::Profile::BackgroundActive
        );
    }
}
