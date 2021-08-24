// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};

use serde_json::{from_value, to_value, Value};

use crate::setui::types::{IntlInfo, MicStates, NetworkType, SetUiResult};
use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_settings::{
    self as fsettings, AudioMarker, AudioStreamSettingSource, AudioStreamSettings,
    ConfigurationInterfaces, DeviceState, DisplayMarker, DisplaySettings, InputMarker, InputState,
    IntlMarker, SetupMarker, SetupSettings, Volume,
};
use fuchsia_component::client::connect_to_protocol;
use fuchsia_syslog::macros::fx_log_info;

/// Facade providing access to SetUi interfaces.
#[derive(Debug)]
pub struct SetUiFacade {
    /// Audio proxy that may be optionally provided for testing. The proxy is not cached during
    /// normal operation.
    audio_proxy: Option<fsettings::AudioProxy>,

    /// Optional Display proxy for testing, similar to `audio_proxy`.
    display_proxy: Option<fsettings::DisplayProxy>,

    /// Optional Input proxy for testing, similar to `audio_proxy`.
    input_proxy: Option<fsettings::InputProxy>,
}

impl SetUiFacade {
    pub fn new() -> SetUiFacade {
        SetUiFacade { audio_proxy: None, display_proxy: None, input_proxy: None }
    }

    /// Sets network option used by device setup.
    /// Same action as choosing "Setup over Ethernet [enabled|disabled]" in "Developer options"
    ///
    /// args: accepted args are "ethernet" or "wifi". ex: {"params": "ethernet"}
    pub async fn set_network(&self, args: Value) -> Result<Value, Error> {
        let network_type: NetworkType = from_value(args)?;
        fx_log_info!("set_network input {:?}", network_type);
        let setup_service_proxy = match connect_to_protocol::<SetupMarker>() {
            Ok(proxy) => proxy,
            Err(e) => bail!("Failed to connect to Setup service {:?}.", e),
        };

        let mut settings = SetupSettings::EMPTY;

        match network_type {
            NetworkType::Ethernet => {
                settings.enabled_configuration_interfaces = Some(ConfigurationInterfaces::Ethernet);
            }
            NetworkType::Wifi => {
                settings.enabled_configuration_interfaces = Some(ConfigurationInterfaces::Wifi);
            }
            _ => return Err(format_err!("Network type must either be ethernet or wifi.")),
        }
        // Update network configuration without automatic device reboot.
        // For changes to take effect, either restart basemgr component or reboot device.
        match setup_service_proxy.set2(settings, false).await? {
            Ok(_) => Ok(to_value(SetUiResult::Success)?),
            Err(err) => Err(format_err!("Update network settings failed with err {:?}", err)),
        }
    }

    /// Reports the network option used for setup
    ///
    /// Returns either "ethernet", "wifi" or "unknown".
    pub async fn get_network_setting(&self) -> Result<Value, Error> {
        let setup_service_proxy = match connect_to_protocol::<SetupMarker>() {
            Ok(proxy) => proxy,
            Err(e) => bail!("Failed to connect to Setup service {:?}.", e),
        };
        let setting = setup_service_proxy.watch().await?;
        match setting.enabled_configuration_interfaces {
            Some(ConfigurationInterfaces::Ethernet) => Ok(to_value(NetworkType::Ethernet)?),
            Some(ConfigurationInterfaces::Wifi) => Ok(to_value(NetworkType::Wifi)?),
            _ => Ok(to_value(NetworkType::Unknown)?),
        }
    }

    /// Sets Internationalization values.
    ///
    /// Input should is expected to be type IntlInfo in json.
    /// Example:
    /// {
    ///     "hour_cycle":"H12",
    ///     "locales":[{"id":"he-FR"}],
    ///     "temperature_unit":"Celsius",
    ///     "time_zone_id":"UTC"
    /// }
    pub async fn set_intl_setting(&self, args: Value) -> Result<Value, Error> {
        let intl_info: IntlInfo = from_value(args)?;
        fx_log_info!("Received Intl Settings Request {:?}", intl_info);

        let intl_service_proxy = match connect_to_protocol::<IntlMarker>() {
            Ok(proxy) => proxy,
            Err(e) => bail!("Failed to connect to Intl service {:?}.", e),
        };
        match intl_service_proxy.set(intl_info.into()).await? {
            Ok(_) => Ok(to_value(SetUiResult::Success)?),
            Err(err) => Err(format_err!("Update intl settings failed with err {:?}", err)),
        }
    }

    /// Reads the Internationalization setting.
    ///
    /// Returns IntlInfo in json.
    pub async fn get_intl_setting(&self) -> Result<Value, Error> {
        let intl_service_proxy = match connect_to_protocol::<IntlMarker>() {
            Ok(proxy) => proxy,
            Err(e) => bail!("Failed to connect to Intl service {:?}.", e),
        };
        let intl_info: IntlInfo = intl_service_proxy.watch().await?.into();
        return Ok(to_value(&intl_info)?);
    }

    /// Reports the microphone DeviceState.
    ///
    /// Returns true if mic is muted or false if mic is unmuted.
    pub async fn is_mic_muted(&self) -> Result<Value, Error> {
        let input_proxy = match self.input_proxy.as_ref() {
            Some(proxy) => proxy.clone(),
            None => match connect_to_protocol::<InputMarker>() {
                Ok(proxy) => proxy,
                Err(e) => bail!("isMicMuted - failed to connect to Input service {:?}.", e),
            },
        };

        match input_proxy.watch2().await?.devices {
            Some(input_device) => {
                let mut muted = false;
                let device = input_device.into_iter().find(|device| {
                    device.device_type == Some(fsettings::DeviceType::Microphone)
                  }).unwrap();
                match device.state {
                    Some(state) => {
                        muted = state.toggle_flags == Some(fsettings::ToggleStateFlags::Muted);
                    },
                    _ => (),
                }
                return Ok(to_value(muted)?);
            }
            _ => Err(format_err!("isMicMuted - cannot read input state.")),
        }
    }

    /// Sets the display brightness via `fuchsia.settings.Display.Set`.
    ///
    /// # Arguments
    /// * `args`: JSON value containing the desired brightness level as f32.
    pub async fn set_brightness(&self, args: Value) -> Result<Value, Error> {
        let brightness: f32 = from_value(args)?;

        // Use the test proxy if one was provided, otherwise connect to the discoverable Display
        // service.
        let display_proxy = match self.display_proxy.as_ref() {
            Some(proxy) => proxy.clone(),
            None => match connect_to_protocol::<DisplayMarker>() {
                Ok(proxy) => proxy,
                Err(e) => bail!("Failed to connect to Display service {:?}.", e),
            },
        };

        let settings = DisplaySettings {
            auto_brightness: Some(false),
            brightness_value: Some(brightness),
            ..DisplaySettings::EMPTY
        };
        match display_proxy.set(settings).await? {
            Ok(_) => Ok(to_value(SetUiResult::Success)?),
            Err(e) => Err(format_err!("SetBrightness failed with err {:?}", e)),
        }
    }

    /// Sets the media volume level via `fuchsia.settings.Audio.Set`.
    ///
    /// # Arguments
    /// * `args`: JSON value containing the desired volume level as f32.
    pub async fn set_media_volume(&self, args: Value) -> Result<Value, Error> {
        let volume: f32 = from_value(args)?;

        // Use the test proxy if one was provided, otherwise connect to the discoverable Audio
        // service.
        let audio_proxy = match self.audio_proxy.as_ref() {
            Some(proxy) => proxy.clone(),
            None => match connect_to_protocol::<AudioMarker>() {
                Ok(proxy) => proxy,
                Err(e) => bail!("Failed to connect to Display service {:?}.", e),
            },
        };

        let stream_settings = AudioStreamSettings {
            stream: Some(AudioRenderUsage::Media),
            source: Some(AudioStreamSettingSource::User),
            user_volume: Some(Volume { level: Some(volume), muted: Some(false), ..Volume::EMPTY }),
            ..AudioStreamSettings::EMPTY
        };
        let settings = fsettings::AudioSettings {
            streams: Some(vec![stream_settings]),
            input: None,
            ..fsettings::AudioSettings::EMPTY
        };

        fx_log_info!("Setting audio settings {:?}", settings);
        match audio_proxy.set(settings).await? {
            Ok(_) => Ok(to_value(SetUiResult::Success)?),
            Err(e) => Err(format_err!("SetVolume failed with err {:?}", e)),
        }
    }

    /// Sets the AudioInput mic to (not)muted depending on input.
    ///
    /// # Arguments
    /// * args: accepted args are "muted" or "available". ex: {"params": "muted"}
    pub async fn set_mic_mute(&self, args: Value) -> Result<Value, Error> {
        let mic_state: MicStates = from_value(args)?;

        // If mic is already in desired state, then nothing left to execute.
        let is_muted = self.is_mic_muted().await?.as_bool().unwrap();
        let mut mute_mic: bool = false;
        match mic_state {
            MicStates::Muted => {
                if is_muted {
                    return Ok(to_value(SetUiResult::Success)?);
                }
                mute_mic = true;
            }
            MicStates::Available => {
                if !is_muted {
                    return Ok(to_value(SetUiResult::Success)?);
                }
            }
            _ => return Err(format_err!("Mic state must either be muted or available.")),
        }

        // Use given proxy (if possible), else connect to protocol.
        let input_proxy = match self.input_proxy.as_ref() {
            Some(proxy) => proxy.clone(),
            None => match connect_to_protocol::<InputMarker>() {
                Ok(proxy) => proxy,
                Err(e) => bail!("Failed to connect to Microphone {:?}.", e),
            },
        };

        // Initialize the InputState struct.
        let mic_device_name = "microphone";
        let mut input_states = InputState {
            name: Some(mic_device_name.to_string()),
            device_type: Some(fsettings::DeviceType::Microphone),
            state: Some(DeviceState {
                toggle_flags: Some(fsettings::ToggleStateFlags::Available),
                ..DeviceState::EMPTY
            }),
            ..InputState::EMPTY
        };

        // Change DeviceState if microphone should be muted- dependent on input enum.
        if mute_mic {
            input_states.state = Some(DeviceState {
                toggle_flags: Some(fsettings::ToggleStateFlags::Muted),
                ..DeviceState::EMPTY
            });
        }

        fx_log_info!("SetMicMute: setting input state {:?}", input_states);
        match input_proxy.set_states(&mut vec![input_states].into_iter()).await? {
            Ok(_) => Ok(to_value(SetUiResult::Success)?),
            Err(e) => Err(format_err!("SetMicMute failed with err {:?}", e)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common_utils::test::assert_value_round_trips_as;
    use crate::setui::types::{
        HourCycle, IntlInfo, LocaleId, MicStates, MicStates::Muted, TemperatureUnit,
    };
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_settings::InputDevice;
    use fuchsia_async as fasync;
    use futures::TryStreamExt;
    use serde_json::json;

    fn make_intl_info() -> IntlInfo {
        return IntlInfo {
            locales: Some(vec![LocaleId { id: "en-US".into() }]),
            temperature_unit: Some(TemperatureUnit::Celsius),
            time_zone_id: Some("UTC".into()),
            hour_cycle: Some(HourCycle::H12),
        };
    }

    #[test]
    fn serde_intl_set() {
        let intl_request = make_intl_info();
        assert_value_round_trips_as(
            intl_request,
            json!(
            {
                "locales": [{"id": "en-US"}],
                "temperature_unit":"Celsius",
                "time_zone_id": "UTC",
                "hour_cycle": "H12",
            }),
        );
    }

    // Tests that `set_brightness` correctly sends a request to the Display service.
    #[fasync::run_singlethreaded(test)]
    async fn test_set_brightness() {
        let brightness = 0.5f32;
        let (proxy, mut stream) = create_proxy_and_stream::<DisplayMarker>().unwrap();

        // Create a facade future that sends a request to `proxy`.
        let facade =
            SetUiFacade { audio_proxy: None, display_proxy: Some(proxy), input_proxy: None };
        let facade_fut = async move {
            assert_eq!(
                facade.set_brightness(to_value(brightness).unwrap()).await.unwrap(),
                to_value(SetUiResult::Success).unwrap()
            );
        };

        // Create a future to service the request stream.
        let stream_fut = async move {
            match stream.try_next().await {
                Ok(Some(fsettings::DisplayRequest::Set { settings, responder })) => {
                    assert_eq!(
                        settings,
                        DisplaySettings {
                            auto_brightness: Some(false),
                            brightness_value: Some(brightness),
                            ..DisplaySettings::EMPTY
                        }
                    );
                    responder.send(&mut Ok(())).unwrap();
                }
                other => panic!("Unexpected stream item: {:?}", other),
            }
        };

        futures::future::join(facade_fut, stream_fut).await;
    }

    // Tests that `set_media_volume` correctly sends a request to the Audio service.
    #[fasync::run_singlethreaded(test)]
    async fn test_set_media_volume() {
        let volume = 0.5f32;
        let (proxy, mut stream) = create_proxy_and_stream::<AudioMarker>().unwrap();

        // Create a facade future that sends a request to `proxy`.
        let facade =
            SetUiFacade { audio_proxy: Some(proxy), display_proxy: None, input_proxy: None };
        let facade_fut = async move {
            assert_eq!(
                facade.set_media_volume(to_value(volume).unwrap()).await.unwrap(),
                to_value(SetUiResult::Success).unwrap()
            );
        };

        // Create a future to service the request stream.
        let stream_fut = async move {
            match stream.try_next().await {
                Ok(Some(fsettings::AudioRequest::Set { settings, responder })) => {
                    let mut streams = settings.streams.unwrap();
                    assert_eq!(1, streams.len());
                    assert_eq!(
                        streams.pop().unwrap(),
                        AudioStreamSettings {
                            stream: Some(AudioRenderUsage::Media),
                            source: Some(AudioStreamSettingSource::User),
                            user_volume: Some(Volume {
                                level: Some(volume),
                                muted: Some(false),
                                ..Volume::EMPTY
                            }),
                            ..AudioStreamSettings::EMPTY
                        }
                    );
                    responder.send(&mut Ok(())).unwrap();
                }
                other => panic!("Unexpected stream item: {:?}", other),
            }
        };

        futures::future::join(facade_fut, stream_fut).await;
    }

    // Tests that `set_mic_mute` correctly sends a request to the Input service to 
    // mute the device mic.
    #[fasync::run_singlethreaded(test)]
    async fn test_set_mic_mute() {
        let mic_state: MicStates = Muted;
        let (proxy, mut stream) = create_proxy_and_stream::<InputMarker>().unwrap();

        // Create a facade future that sends a request to `proxy`.
        let facade =
            SetUiFacade { audio_proxy: None, display_proxy: None, input_proxy: Some(proxy) };
        let facade_fut = async move {
            assert_eq!(
                facade.set_mic_mute(to_value(mic_state).unwrap()).await.unwrap(),
                to_value(SetUiResult::Success).unwrap()
            );
        };

        // Create a future to service the request stream.
        let input_stream_fut = async move {
            match stream.try_next().await {
                Ok(Some(fsettings::InputRequest::Watch2 { responder })) => {
                    let device = InputDevice {
                        device_name: None,
                        device_type: Some(fsettings::DeviceType::Microphone),
                        source_states: None,
                        mutable_toggle_state: None,
                        state: Some(DeviceState {
                            toggle_flags: Some(fsettings::ToggleStateFlags::Available),
                            ..DeviceState::EMPTY
                        }),
                        unknown_data: None,
                        ..InputDevice::EMPTY
                    };
                    let settings = fsettings::InputSettings {
                        devices: Some(vec![device]),
                        unknown_data: None,
                        ..fsettings::InputSettings::EMPTY
                    };
                    responder.send(settings).unwrap();
                }
                other => panic!("Unexpected Watch2 request: {:?}", other),
            }
            match stream.try_next().await {
                Ok(Some(fsettings::InputRequest::SetStates { input_states, responder })) => {
                    assert_eq!(
                        input_states[0],
                        InputState {
                            name: Some("microphone".to_string()),
                            device_type: Some(fsettings::DeviceType::Microphone),
                            state: Some(DeviceState {
                                toggle_flags: Some(fsettings::ToggleStateFlags::Muted),
                                ..DeviceState::EMPTY
                            }),
                            ..InputState::EMPTY
                        }
                    );
                    responder.send(&mut Ok(())).unwrap();
                }
                other => panic!("Unexpected stream item: {:?}", other),
            }
        };

        futures::future::join(facade_fut, input_stream_fut).await;
    }

    // Tests that `set_mic_mute` does not send a request to the Input service if the mic is already in desired state.
    #[fasync::run_singlethreaded(test)]
    async fn test_set_mic_mute_in_desired_state() {
        let mic_state: MicStates = Muted;
        let (proxy, mut stream) = create_proxy_and_stream::<InputMarker>().unwrap();

        // Create a facade future that sends a request to `proxy`.
        let facade =
            SetUiFacade { audio_proxy: None, display_proxy: None, input_proxy: Some(proxy) };
        let facade_fut = async move {
            assert_eq!(
                facade.set_mic_mute(to_value(mic_state).unwrap()).await.unwrap(),
                to_value(SetUiResult::Success).unwrap()
            );
        };

        // Create a future to check that the request stream using SetStates is never called (due to early termination).
        let input_stream_fut = async move {
            match stream.try_next().await {
                Ok(Some(fsettings::InputRequest::Watch2 { responder })) => {
                    let device = InputDevice {
                        device_name: None,
                        device_type: Some(fsettings::DeviceType::Microphone),
                        source_states: None,
                        mutable_toggle_state: None,
                        state: Some(DeviceState {
                            toggle_flags: Some(fsettings::ToggleStateFlags::Muted),
                            ..DeviceState::EMPTY
                        }),
                        unknown_data: None,
                        ..InputDevice::EMPTY
                    };
                    let settings = fsettings::InputSettings {
                        devices: Some(vec![device]),
                        unknown_data: None,
                        ..fsettings::InputSettings::EMPTY
                    };
                    responder.send(settings).unwrap();
                }
                other => panic!("Unexpected Watch2 request: {:?}", other),
            }
            match stream.try_next().await {
                Ok(Some(fsettings::InputRequest::SetStates { input_states, responder: _ })) => {
                    panic!("Unexpected stream item: {:?}", input_states[0]);
                }
                _ => (),
            }
        };

        futures::future::join(facade_fut, input_stream_fut).await;
    }

    // Tests that `is_mic_muted` correctly returns the mic state.
    #[fasync::run_singlethreaded(test)]
    async fn test_is_mic_muted() {
        let is_muted = true;
        let (proxy, mut stream) = create_proxy_and_stream::<InputMarker>().unwrap();

        // Create a facade future that sends a request to `proxy`.
        let facade =
            SetUiFacade { audio_proxy: None, display_proxy: None, input_proxy: Some(proxy) };
        let facade_fut = async move {
            assert_eq!(
                facade.is_mic_muted().await.unwrap(),
                to_value(is_muted).unwrap()
            );
        };

        // Create a future to service the request stream.
        let input_stream_fut = async move {
            match stream.try_next().await {
                Ok(Some(fsettings::InputRequest::Watch2 { responder })) => {
                    let device = InputDevice {
                        device_name: None,
                        device_type: Some(fsettings::DeviceType::Microphone),
                        source_states: None,
                        mutable_toggle_state: None,
                        state: Some(DeviceState {
                            toggle_flags: Some(fsettings::ToggleStateFlags::Muted),
                            ..DeviceState::EMPTY
                        }),
                        unknown_data: None,
                        ..InputDevice::EMPTY
                    };
                    let settings = fsettings::InputSettings {
                        devices: Some(vec![device]),
                        unknown_data: None,
                        ..fsettings::InputSettings::EMPTY
                    };
                    responder.send(settings).unwrap();
                }
                other => panic!("Unexpected Watch2 request: {:?}", other),
            }
        };

        futures::future::join(facade_fut, input_stream_fut).await;
    }
}
