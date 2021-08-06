// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::log_if_err,
    crate::message::Message,
    crate::node::Node,
    anyhow::{format_err, Context, Result},
    async_trait::async_trait,
    fidl_fuchsia_ui_activity as factivity,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_inspect::{self as inspect, Property},
    futures::future::{FutureExt as _, LocalBoxFuture},
    futures::stream::FuturesUnordered,
    futures::StreamExt as _,
    serde_derive::Deserialize,
    serde_json as json,
    std::collections::HashMap,
    std::rc::Rc,
};

/// Node: ActivityHandler
///
/// Summary: Connects to the Activity service to monitor for user activity state changes in the
///          system. Relays these activity changes to the SystemProfileHandler node.
///
/// Handles Messages: N/A
///
/// Sends Messages:
///     - NotifyUserActiveChanged
///
/// FIDL dependencies:
///     - fuchsia.ui.activity.Provider: the node connects to this service and passes a
///       fuchsia.ui.activity.Listener channel to receive callback notifications when the user
///       activity state of the system has changed.

pub struct ActivityHandlerBuilder<'a> {
    profile_handler_node: Rc<dyn Node>,
    activity_proxy: Option<factivity::ProviderProxy>,
    inspect_root: Option<&'a inspect::Node>,
}

impl<'a> ActivityHandlerBuilder<'a> {
    pub fn new_from_json(json_data: json::Value, nodes: &HashMap<String, Rc<dyn Node>>) -> Self {
        #[derive(Deserialize)]
        struct Dependencies {
            system_profile_handler_node: String,
        }

        #[derive(Deserialize)]
        struct JsonData {
            dependencies: Dependencies,
        }

        let data: JsonData = json::from_value(json_data).unwrap();
        Self {
            profile_handler_node: nodes[&data.dependencies.system_profile_handler_node].clone(),
            activity_proxy: None,
            inspect_root: None,
        }
    }

    #[cfg(test)]
    fn new(profile_handler_node: Rc<dyn Node>) -> Self {
        Self { profile_handler_node, activity_proxy: None, inspect_root: None }
    }

    #[cfg(test)]
    fn with_inspect_root(mut self, root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(root);
        self
    }

    #[cfg(test)]
    fn with_proxy(mut self, proxy: factivity::ProviderProxy) -> Self {
        self.activity_proxy = Some(proxy);
        self
    }

    pub fn build(
        self,
        futures_out: &FuturesUnordered<LocalBoxFuture<'_, ()>>,
    ) -> Result<Rc<ActivityHandler>> {
        // Allow test to override
        let inspect_root =
            self.inspect_root.unwrap_or_else(|| inspect::component::inspector().root());
        let inspect = InspectData::new(inspect_root, "ActivityHandler".to_string());

        // Allow test to override
        let activity_proxy = if let Some(proxy) = self.activity_proxy {
            proxy
        } else {
            connect_to_protocol::<factivity::ProviderMarker>()?
        };

        let node = Rc::new(ActivityHandler {
            activity_proxy,
            profile_handler_node: self.profile_handler_node,
            inspect,
        });

        futures_out.push(node.clone().watch_activity()?);

        Ok(node)
    }
}

pub struct ActivityHandler {
    /// Proxy to the fuchsia.ui.activity.Provider service.
    activity_proxy: factivity::ProviderProxy,

    /// Node that we send the NotifyUserActiveChanged message to once we observe changes to the
    /// activity state.
    profile_handler_node: Rc<dyn Node>,

    inspect: InspectData,
}

impl ActivityHandler {
    /// Watch the Activity service for changes to the activity state. When changes are observed, a
    /// NotifyUserActiveChanged message is sent to `profile_handler_node`. The method returns a
    /// Future that performs these steps in an infinite loop.
    fn watch_activity<'a>(self: Rc<Self>) -> Result<LocalBoxFuture<'a, ()>> {
        // Connect a Listener channel with the service. We'll poll `listener_stream` for activity
        // state change events.
        let mut listener_stream = Self::connect_activity_service(&self.activity_proxy)?;

        Ok(async move {
            self.inspect.set_handler_enabled(true);
            let mut prev_is_active = None;
            loop {
                match listener_stream.next().await {
                    // Got a state change event
                    Some(Ok(factivity::ListenerRequest::OnStateChanged {
                        state,
                        responder,
                        ..
                    })) => {
                        let is_active = state == factivity::State::Active;
                        if prev_is_active != Some(is_active) {
                            self.inspect.set_active(is_active);
                            prev_is_active = Some(is_active);
                            log_if_err!(
                                self.send_message(
                                    &self.profile_handler_node,
                                    &Message::NotifyUserActiveChanged(is_active),
                                )
                                .await,
                                "Failed to send NotifyUserActiveChanged"
                            );
                        }

                        // ack the ListenerRequest
                        let _ = responder.send();
                    }

                    // Stream gave an unexpected error
                    Some(Err(e)) => log::error!("Error polling listener_stream: {}", e),

                    // Stream closed unexpectedly. Attempt to reconnect once. If reconnection fails,
                    // exit the loop because something is wrong (or the Activity service is not
                    // available on this build variant).
                    None => {
                        log::error!("Listener stream closed. Reconnecting...");
                        match Self::connect_activity_service(&self.activity_proxy) {
                            Ok(stream) => listener_stream = stream,
                            Err(e) => {
                                log::error!("{}", e);
                                break;
                            }
                        }
                    }
                }
            }

            log::error!("ActivityHandler is disabled");
            self.inspect.set_handler_enabled(false);
        }
        .boxed_local())
    }

    /// Creates a Listener channel pair and passes the client end to the Activity service. Activity
    /// state updates will arrive via the Listener stream.
    fn connect_activity_service(
        activity_provider: &factivity::ProviderProxy,
    ) -> Result<factivity::ListenerRequestStream> {
        let (client, stream) =
            fidl::endpoints::create_request_stream::<factivity::ListenerMarker>()
                .context("Failed to create request stream")?;
        activity_provider
            .watch_state(client)
            .map_err(|e| format_err!("watch_state failed: {:?}", e))?;
        Ok(stream)
    }
}

#[async_trait(?Send)]
impl Node for ActivityHandler {
    fn name(&self) -> String {
        "ActivityHandler".to_string()
    }
}

struct InspectData {
    handler_enabled: inspect::StringProperty,
    activity_state: inspect::StringProperty,
}

impl InspectData {
    fn new(parent: &inspect::Node, name: String) -> Self {
        let root = parent.create_child(name);
        let handler_enabled = root.create_string("handler_enabled", "");
        let activity_state = root.create_string("activity_state", "");
        parent.record(root);
        Self { handler_enabled, activity_state }
    }

    fn set_handler_enabled(&self, enabled: bool) {
        self.handler_enabled.set(&enabled.to_string());
    }

    fn set_active(&self, active: bool) {
        self.activity_state.set(match active {
            true => "active",
            false => "inactive",
        });
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::test::mock_node::{create_dummy_node, MessageMatcher, MockNodeMaker},
        crate::{msg_eq, msg_ok_return},
        fuchsia_async as fasync,
        fuchsia_inspect::assert_data_tree,
    };

    // A fake Activity provider service implementation for testing
    struct FakeActivityProvider {
        provider_stream: factivity::ProviderRequestStream,
        listener_proxy: Option<factivity::ListenerProxy>,
    }

    impl FakeActivityProvider {
        fn new() -> (factivity::ProviderProxy, Self) {
            let (provider_proxy, provider_stream) =
                fidl::endpoints::create_proxy_and_stream::<factivity::ProviderMarker>()
                    .expect("Failed to create ActivityProvider proxy and stream");

            (provider_proxy, Self { provider_stream, listener_proxy: None })
        }

        // Gets the Listener proxy object that was provided by the ActivityHandler.
        async fn listener_proxy(&mut self) -> factivity::ListenerProxy {
            if let Some(proxy) = &self.listener_proxy {
                proxy.clone()
            } else {
                let proxy = match self
                    .provider_stream
                    .next()
                    .await
                    .expect(
                        "Provider request stream yielded Some(None) (provider channel closed without
                        adding a Listener via the watch_state call)",
                    )
                    .expect("Provider request stream yielded Some(Err)")
                {
                    factivity::ProviderRequest::WatchState { listener, .. } => {
                        listener.into_proxy().expect("Failed to convert ClientEnd into proxy")
                    }
                };

                self.listener_proxy = Some(proxy.clone());
                proxy
            }
        }

        // Send the activity state update to the Listener channel.
        async fn set_user_active(&mut self, active: bool) {
            let state = if active { factivity::State::Active } else { factivity::State::Idle };
            self.listener_proxy()
                .await
                .on_state_changed(state, 0)
                .await
                .expect("Failed to send on_state_changed");
        }
    }

    /// Tests for the presence and correctness of dynamically-added inspect data
    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_data() {
        let inspector = inspect::Inspector::new();
        let (provider_proxy, mut fake_activity) = FakeActivityProvider::new();
        let futures_out = FuturesUnordered::new();
        let _node = ActivityHandlerBuilder::new(create_dummy_node())
            .with_inspect_root(inspector.root())
            .with_proxy(provider_proxy)
            .build(&futures_out)
            .unwrap();

        // Use `select!` here so that the node future is polled concurrently with our fake_activity
        // changes. This lets the node's `watch_activity` future run while we simulate activity
        // state changes.
        futures::select! {
            _ = futures_out.collect::<()>() => {},
            _ = async {
                fake_activity.set_user_active(false).await;
                assert_data_tree!(
                    inspector,
                    root: {
                        "ActivityHandler": {
                            "handler_enabled": "true",
                            "activity_state": "inactive"
                        }
                    }
                );

                fake_activity.set_user_active(true).await;
                assert_data_tree!(
                    inspector,
                    root: {
                        "ActivityHandler": {
                            "handler_enabled": "true",
                            "activity_state": "active"
                        }
                    }
                );
            }.fuse() => {}
        };
    }

    /// Tests that the ActivityHandler relays NotifyUserActiveChanged messages to the ProfileHandler
    /// node when it observes changes to the activity state.
    #[fasync::run_singlethreaded(test)]
    async fn test_activity_monitor() {
        let mut mock_maker = MockNodeMaker::new();

        // For this test, the ProfileHandler should receive two NotifyUserActiveChanged messages
        let profile_handler_node = mock_maker.make(
            "ProfileHandler",
            vec![
                (msg_eq!(NotifyUserActiveChanged(true)), msg_ok_return!(NotifyUserActiveChanged)),
                (msg_eq!(NotifyUserActiveChanged(false)), msg_ok_return!(NotifyUserActiveChanged)),
            ],
        );

        // Create the node
        let (provider_proxy, mut fake_activity) = FakeActivityProvider::new();
        let futures_out = FuturesUnordered::new();
        let _node = ActivityHandlerBuilder::new(profile_handler_node)
            .with_proxy(provider_proxy)
            .build(&futures_out);

        // Use `select!` here so that the node future is polled concurrently with our fake_activity
        // changes. This lets the node's `watch_activity` future run while we simulate activity
        // state changes.
        futures::select! {
            _ = futures_out.collect::<()>() => {},
            _ = async {
                fake_activity.set_user_active(true).await;
                fake_activity.set_user_active(false).await;
                fake_activity.set_user_active(false).await;
            }.fuse() => {}
        };

        // When `mock_maker` goes out of scope it verifies the two NotifyUserActiveChanged messages
        // were received
    }
}
