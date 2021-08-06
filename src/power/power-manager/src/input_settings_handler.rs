// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::log_if_err,
    crate::message::Message,
    crate::node::Node,
    anyhow::{format_err, Context, Result},
    async_trait::async_trait,
    async_utils::hanging_get::client::HangingGetStream,
    fidl_fuchsia_settings as fsettings,
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

/// Node: InputSettingsHandler
///
/// Summary: Connects to the fuchsia.settings.Input service to monitor for input settings changes.
///          (Initially, the node is only concerned with monitoring the enabled state of the
///          microphone). The node relays these settings changes to the SystemProfileHandler node.
///
/// Handles Messages: N/A
///
/// Sends Messages:
///     - NotifyMicEnabledChanged
///
/// FIDL dependencies:
///     - fuchsia.settings.Input: the node connects to this service to monitor for changes to input
///       settings

pub struct InputSettingsHandlerBuilder<'a> {
    profile_handler_node: Rc<dyn Node>,
    input_settings_proxy: Option<fsettings::InputProxy>,
    inspect_root: Option<&'a inspect::Node>,
}

impl<'a> InputSettingsHandlerBuilder<'a> {
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
            input_settings_proxy: None,
            inspect_root: None,
        }
    }

    #[cfg(test)]
    fn new(profile_handler_node: Rc<dyn Node>) -> Self {
        Self { profile_handler_node, input_settings_proxy: None, inspect_root: None }
    }

    #[cfg(test)]
    fn with_inspect_root(mut self, root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(root);
        self
    }

    #[cfg(test)]
    fn with_proxy(mut self, proxy: fsettings::InputProxy) -> Self {
        self.input_settings_proxy = Some(proxy);
        self
    }

    pub fn build(
        self,
        futures_out: &FuturesUnordered<LocalBoxFuture<'_, ()>>,
    ) -> Result<Rc<InputSettingsHandler>> {
        // Allow test to override
        let inspect_root =
            self.inspect_root.unwrap_or_else(|| inspect::component::inspector().root());
        let inspect = InspectData::new(inspect_root, "InputSettingsHandler".to_string());

        // Allow test to override
        let input_settings_proxy = if let Some(proxy) = self.input_settings_proxy {
            proxy
        } else {
            connect_to_protocol::<fsettings::InputMarker>()?
        };

        let node = Rc::new(InputSettingsHandler {
            input_settings_proxy,
            profile_handler_node: self.profile_handler_node,
            inspect,
        });

        futures_out.push(node.clone().watch_input_settings()?);

        Ok(node)
    }
}

pub struct InputSettingsHandler {
    /// Proxy to the fuchsia.settings.Input service.
    input_settings_proxy: fsettings::InputProxy,

    /// Node that we send the NotifyMicEnabledChanged message to once we observe changes to the
    /// input settings.
    profile_handler_node: Rc<dyn Node>,

    inspect: InspectData,
}

impl InputSettingsHandler {
    /// Watch the Input settings service for changes. When changes to the microphone enabled state
    /// are observed, a NotifyMicEnabledChanged message is sent to `profile_handler_node`. The
    /// method returns a Future that performs these steps in an infinite loop.
    fn watch_input_settings<'a>(self: Rc<Self>) -> Result<LocalBoxFuture<'a, ()>> {
        // Create a HangingGetStream wrapper to abstract the details of the hanging-get pattern that
        // is used by the InputSettings service.
        let proxy = self.input_settings_proxy.clone();
        let mut stream = HangingGetStream::new(Box::new(move || Some(proxy.watch2())));

        Ok(async move {
            self.inspect.set_handler_enabled(true);
            let mut prev_mic_enabled = None;
            loop {
                match stream.next().await {
                    // Got a settings change event
                    Some(Ok(settings)) => match Self::parse_is_mic_enabled(settings) {
                        Ok(enabled) => {
                            if prev_mic_enabled != Some(enabled) {
                                self.inspect.set_mic_enabled(enabled);
                                prev_mic_enabled = Some(enabled);
                                log_if_err!(
                                    self.send_message(
                                        &self.profile_handler_node,
                                        &Message::NotifyMicEnabledChanged(enabled),
                                    )
                                    .await,
                                    "Failed to send NotifyMicEnabledChanged"
                                );
                            }
                        }
                        Err(e) => log::error!("Failed to parse mic settings: {:?}", e),
                    },

                    // Stream gave an unexpected error. This should only happen if the InputSettings
                    // service is not available (likely because it isn't running on this build
                    // variant), so exit the loop.
                    Some(Err(e)) => {
                        log::error!("Failed to monitor fuchsia.settings.Input ({:?})", e);
                        break;
                    }

                    // Stream will never close because the HangingGetStream always polls for new
                    // data.
                    None => unreachable!(),
                }
            }

            log::error!("InputSettingsHandler is disabled");
            self.inspect.set_handler_enabled(false);
        }
        .boxed_local())
    }

    /// Parses the InputSettings struct to retrieve microphone enabled state.
    fn parse_is_mic_enabled(settings: fsettings::InputSettings) -> Result<bool> {
        let mic_settings = settings
            .devices
            .context("Missing 'devices' in settings")?
            .into_iter()
            .filter(|device| device.device_type == Some(fsettings::DeviceType::Microphone))
            .collect::<Vec<_>>();

        match mic_settings.len() {
            0 => Err(format_err!("Missing microphone settings")),
            1 => Ok(()),
            n => Err(format_err!("Invalid microphone settings length {} (expected 1)", n)),
        }?;

        let is_enabled = mic_settings[0]
            .state
            .as_ref()
            .ok_or(format_err!("Microphone DeviceState is None"))?
            .toggle_flags
            .ok_or(format_err!("Microphone ToggleStateFlags is None"))?
            .contains(fsettings::ToggleStateFlags::Available);

        Ok(is_enabled)
    }
}

#[async_trait(?Send)]
impl Node for InputSettingsHandler {
    fn name(&self) -> String {
        "InputSettingsHandler".to_string()
    }
}

struct InspectData {
    handler_enabled: inspect::StringProperty,
    mic_enabled: inspect::StringProperty,
}

impl InspectData {
    fn new(parent: &inspect::Node, name: String) -> Self {
        let root = parent.create_child(name);
        let handler_enabled = root.create_string("handler_enabled", "");
        let mic_enabled = root.create_string("mic_enabled", "");
        parent.record(root);
        Self { handler_enabled, mic_enabled }
    }

    fn set_handler_enabled(&self, enabled: bool) {
        self.handler_enabled.set(&enabled.to_string());
    }

    fn set_mic_enabled(&self, enabled: bool) {
        self.mic_enabled.set(&enabled.to_string());
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

    // A fake Settings service implementation for testing
    struct FakeSettingsSvc {
        stream: fsettings::InputRequestStream,
        pending_request: Option<fsettings::InputWatch2Responder>,
    }
    impl FakeSettingsSvc {
        fn new() -> (fsettings::InputProxy, Self) {
            let (proxy, stream) =
                fidl::endpoints::create_proxy_and_stream::<fsettings::InputMarker>()
                    .expect("Failed to create Input proxy and stream");
            (proxy, Self { stream, pending_request: None })
        }

        // Generates the required InputSettings struct based on some short parameters for testing
        fn generate_device_settings(mic_enabled: bool) -> fsettings::InputSettings {
            fsettings::InputSettings {
                devices: Some(vec![fsettings::InputDevice {
                    device_type: Some(fsettings::DeviceType::Microphone),
                    state: Some(fsettings::DeviceState {
                        toggle_flags: Some(if mic_enabled {
                            fsettings::ToggleStateFlags::Available
                        } else {
                            fsettings::ToggleStateFlags::Muted
                        }),
                        ..fsettings::DeviceState::EMPTY
                    }),
                    ..fsettings::InputDevice::EMPTY
                }]),
                ..fsettings::InputSettings::EMPTY
            }
        }

        // Gets the pending hanging-get request and completes the request with the specified device
        // settings. Waits for the next hanging-get request to arrive before returning to ensure the
        // node has processed the response.
        async fn set_mic_enabled(&mut self, enabled: bool) {
            // Make sure there is a pending hanging-get
            self.ensure_request_pending().await;

            // Complete the pending hanging-get with the mic_enabled value
            self.pending_request
                .take()
                .unwrap()
                .send(Self::generate_device_settings(enabled))
                .expect("Failed to send mic state update to client");

            // Wait for the next hanging-get request to arrive so we can be sure the node has
            // processed the mic_enabled result we just provided
            self.ensure_request_pending().await;
        }

        async fn ensure_request_pending(&mut self) {
            if self.pending_request.is_none() {
                self.pending_request = Some(self.get_next_request().await);
            }
        }

        // Retrieves the next hanging-get request
        async fn get_next_request(&mut self) -> fsettings::InputWatch2Responder {
            match self
                .stream
                .next()
                .await
                .expect(
                    "Input request stream yielded Some(None)
                    (Input channel closed without receiving hanging-get request)",
                )
                .expect("Input request stream yielded Some(Err)")
            {
                fsettings::InputRequest::Watch2 { responder } => responder,
                request => panic!("Unexpected request: {:?}", request),
            }
        }
    }

    /// Tests for the presence and correctness of dynamically-added inspect data
    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_data() {
        let inspector = inspect::Inspector::new();
        let (proxy, mut fake_settings) = FakeSettingsSvc::new();
        let futures_out = FuturesUnordered::new();
        let _node = InputSettingsHandlerBuilder::new(create_dummy_node())
            .with_inspect_root(inspector.root())
            .with_proxy(proxy)
            .build(&futures_out)
            .unwrap();

        futures::select! {
            _ = futures_out.collect::<()>() => {},
            _ = async {
                fake_settings.set_mic_enabled(true).await;
                assert_data_tree!(
                    inspector,
                    root: {
                        "InputSettingsHandler": {
                            "handler_enabled": "true",
                            "mic_enabled": "true"
                        }
                    }
                );

                fake_settings.set_mic_enabled(false).await;
                assert_data_tree!(
                    inspector,
                    root: {
                        "InputSettingsHandler": {
                            "handler_enabled": "true",
                            "mic_enabled": "false"
                        }
                    }
                );
            }.fuse() => {}
        };
    }

    /// Tests that the InputSettingsHandler relays NotifyMicEnabledChanged messages to the
    /// ProfileHandler node when it observes changes to input settings.
    #[fasync::run_singlethreaded(test)]
    async fn test_settings_monitor() {
        let mut mock_maker = MockNodeMaker::new();

        // For this test, the ProfileHandler should receive two NotifyMicEnabledChanged messages
        let profile_handler_node = mock_maker.make(
            "ProfileHandler",
            vec![
                (msg_eq!(NotifyMicEnabledChanged(true)), msg_ok_return!(NotifyMicEnabledChanged)),
                (msg_eq!(NotifyMicEnabledChanged(false)), msg_ok_return!(NotifyMicEnabledChanged)),
            ],
        );

        // Create the node
        let (proxy, mut fake_settings) = FakeSettingsSvc::new();
        let futures_out = FuturesUnordered::new();
        let _node = InputSettingsHandlerBuilder::new(profile_handler_node)
            .with_proxy(proxy)
            .build(&futures_out);

        // Use `select!` here so that the node future is polled concurrently with our fake_settings
        // changes. This lets the node's `watch_input_settings` future run while we simulate input
        // settings changes.
        futures::select! {
            _ = futures_out.collect::<()>() => {},
            _ = async {
                fake_settings.set_mic_enabled(true).await;
                fake_settings.set_mic_enabled(false).await;
            }.fuse() => {}
        };

        // When mock_maker goes out of scope it verifies the two NotifyMicEnabledChanged messages
        // were received
    }
}
