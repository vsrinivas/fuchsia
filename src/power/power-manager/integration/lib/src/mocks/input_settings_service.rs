// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_settings as fsettings, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::LocalComponentHandles,
    futures::{channel::mpsc, lock::Mutex, StreamExt, TryStreamExt},
    std::sync::Arc,
    tracing::*,
};

/// Mocks the fuchsia.settings.Input service to be used in integration tests.
pub struct MockInputSettingsService {
    /// Sends new input settings to the mock server. The expected usage is that the test holds the
    /// sender end to communicate new input settings to the server on the receiver end.
    settings_sender: Mutex<mpsc::Sender<fsettings::InputSettings>>,

    /// Receiver end for input settings changes. When the server reads the new settings from the
    /// receiver end, it will send those new settings out to any pending clients that have
    /// previously called `Watch`.
    settings_receiver: Mutex<mpsc::Receiver<fsettings::InputSettings>>,
}

impl MockInputSettingsService {
    pub fn new() -> Arc<MockInputSettingsService> {
        let (settings_sender, settings_receiver) = mpsc::channel(1);
        Arc::new(Self {
            settings_sender: Mutex::new(settings_sender),
            settings_receiver: Mutex::new(settings_receiver),
        })
    }

    /// Runs the mock using the provided `LocalComponentHandles`.
    ///
    /// Expected usage is to call this function from a closure for the
    /// `local_component_implementation` parameter to `RealmBuilder.add_local_child`.
    ///
    /// For example:
    ///     let mock_input_settings_service = MockInputSettingsService::new();
    ///     let input_settings_service_child = realm_builder
    ///         .add_local_child(
    ///             "input_settings_service",
    ///             move |handles| {
    ///                 Box::pin(input_settings_service.clone().run(handles))
    ///             },
    ///             ChildOptions::new(),
    ///         )
    ///         .await
    ///         .unwrap();
    ///
    pub async fn run(self: Arc<Self>, handles: LocalComponentHandles) -> Result<(), anyhow::Error> {
        self.run_inner(handles.outgoing_dir).await
    }

    async fn run_inner(
        self: Arc<Self>,
        outgoing_dir: ServerEnd<DirectoryMarker>,
    ) -> Result<(), anyhow::Error> {
        let mut fs = ServiceFs::new();
        fs.dir("svc").add_fidl_service(move |mut stream: fsettings::InputRequestStream| {
            let this = self.clone();

            // Set the initial settings update value for this connection implement hanging-get
            let mut initial_settings_update = Some(generate_device_settings(false));

            fasync::Task::local(async move {
                info!("MockInputSettingsService: new connection");
                while let Some(fsettings::InputRequest::Watch { responder }) =
                    stream.try_next().await.unwrap()
                {
                    info!("MockInputSettingsService: received Watch request");
                    let settings = if let Some(settings) = initial_settings_update.take() {
                        settings
                    } else {
                        this.settings_receiver.lock().await.next().await.unwrap()
                    };

                    info!("MockInputSettingsService: sending input settings: {:?}", settings);
                    let _ = responder.send(settings);
                }

                info!("MockInputSettingsService: closing connection")
            })
            .detach();
        });

        fs.serve_connection(outgoing_dir).unwrap();
        fs.collect::<()>().await;

        Ok(())
    }

    pub async fn set_mic_enabled(&self, mic_enabled: bool) {
        info!("MockInputSettingsService: set mic enabled: {:?}", mic_enabled);
        let input_settings = generate_device_settings(mic_enabled);
        self.settings_sender.lock().await.try_send(input_settings).expect("try_send() failed");
    }
}

fn generate_device_settings(mic_enabled: bool) -> fsettings::InputSettings {
    fsettings::InputSettings {
        devices: Some(vec![fsettings::InputDevice {
            device_type: Some(fsettings::DeviceType::Microphone),
            state: Some(fsettings::DeviceState {
                toggle_flags: Some(if mic_enabled {
                    fsettings::ToggleStateFlags::AVAILABLE
                } else {
                    fsettings::ToggleStateFlags::MUTED
                }),
                ..fsettings::DeviceState::EMPTY
            }),
            ..fsettings::InputDevice::EMPTY
        }]),
        ..fsettings::InputSettings::EMPTY
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_component::client::connect_to_protocol_at_dir_svc};

    /// Parses the InputSettings struct to retrieve microphone enabled state.
    fn parse_is_mic_enabled(settings: fsettings::InputSettings) -> bool {
        let mic_settings = settings
            .devices
            .unwrap()
            .into_iter()
            .filter(|device| device.device_type == Some(fsettings::DeviceType::Microphone))
            .collect::<Vec<_>>();

        assert_eq!(mic_settings.len(), 1);

        let is_enabled = mic_settings[0]
            .state
            .as_ref()
            .unwrap()
            .toggle_flags
            .unwrap()
            .contains(fsettings::ToggleStateFlags::AVAILABLE);

        is_enabled
    }

    #[fuchsia::test]
    async fn test_set_mic_enabled() {
        // Create and serve the mock service
        let (dir, outgoing_dir) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let mock = MockInputSettingsService::new();
        let _task = fasync::Task::local(mock.clone().run_inner(outgoing_dir));

        // Connect to the mock server
        let settings_client =
            connect_to_protocol_at_dir_svc::<fsettings::InputMarker>(&dir).unwrap();

        // mic-enable is initially false
        let settings = settings_client.watch().await.unwrap();
        assert_eq!(parse_is_mic_enabled(settings), false);

        // set the mock mic-enable to true and verify the client gets the updated value
        mock.set_mic_enabled(true).await;
        let settings = settings_client.watch().await.unwrap();
        assert_eq!(parse_is_mic_enabled(settings), true);
    }
}
