// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};

use serde_json::{from_value, to_value, Value};

use crate::setui::types::{IntlInfo, NetworkType, SetUiResult};
use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_settings::{
    self as fsettings, AudioMarker, AudioStreamSettingSource, AudioStreamSettings,
    ConfigurationInterfaces, DisplayMarker, DisplaySettings, IntlMarker, SetupMarker,
    SetupSettings, Volume,
};
use fuchsia_component::client::connect_to_service;
use fuchsia_syslog::macros::fx_log_info;

/// Facade providing access to SetUi interfaces.
#[derive(Debug)]
pub struct SetUiFacade {
    /// Audio proxy that may be optionally provided for testing. The proxy is not cached during
    /// normal operation.
    audio_proxy: Option<fsettings::AudioProxy>,

    /// Optional Display proxy for testing, similar to `audio_proxy`.
    display_proxy: Option<fsettings::DisplayProxy>,
}

impl SetUiFacade {
    pub fn new() -> SetUiFacade {
        SetUiFacade { audio_proxy: None, display_proxy: None }
    }

    /// Sets network option used by device setup.
    /// Same action as choosing "Setup over Ethernet [enabled|disabled]" in "Developer options"
    ///
    /// args: accepted args are "ethernet" or "wifi". ex: {"params": "ethernet"}
    pub async fn set_network(&self, args: Value) -> Result<Value, Error> {
        let network_type: NetworkType = from_value(args)?;
        fx_log_info!("set_network input {:?}", network_type);
        let setup_service_proxy = match connect_to_service::<SetupMarker>() {
            Ok(proxy) => proxy,
            Err(e) => bail!("Failed to connect to Setup service {:?}.", e),
        };

        let mut settings = SetupSettings::empty();

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
        let setup_service_proxy = match connect_to_service::<SetupMarker>() {
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

        let intl_service_proxy = match connect_to_service::<IntlMarker>() {
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
        let intl_service_proxy = match connect_to_service::<IntlMarker>() {
            Ok(proxy) => proxy,
            Err(e) => bail!("Failed to connect to Intl service {:?}.", e),
        };
        let intl_info: IntlInfo = intl_service_proxy.watch().await?.into();
        return Ok(to_value(&intl_info)?);
    }

    /// Reports the AudioInput (mic muted) state.
    ///
    /// Returns true if mic is muted or false if mic is unmuted.
    pub async fn is_mic_muted(&self) -> Result<Value, Error> {
        let audio_proxy = match connect_to_service::<AudioMarker>() {
            Ok(proxy) => proxy,
            Err(e) => bail!("Failed to connect to Setup Audio service {:?}.", e),
        };
        match audio_proxy.watch().await?.input {
            Some(audio_input) => Ok(to_value(audio_input.muted)?),
            _ => Err(format_err!("Cannot read audio input.")),
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
            None => match connect_to_service::<DisplayMarker>() {
                Ok(proxy) => proxy,
                Err(e) => bail!("Failed to connect to Display service {:?}.", e),
            },
        };

        let settings = DisplaySettings {
            auto_brightness: Some(false),
            brightness_value: Some(brightness),
            ..DisplaySettings::empty()
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
            None => match connect_to_service::<AudioMarker>() {
                Ok(proxy) => proxy,
                Err(e) => bail!("Failed to connect to Display service {:?}.", e),
            },
        };

        let stream_settings = AudioStreamSettings {
            stream: Some(AudioRenderUsage::Media),
            source: Some(AudioStreamSettingSource::User),
            user_volume: Some(Volume {
                level: Some(volume),
                muted: Some(false),
                ..Volume::empty()
            }),
            ..AudioStreamSettings::empty()
        };
        let settings = fsettings::AudioSettings {
            streams: Some(vec![stream_settings]),
            input: None,
            ..fsettings::AudioSettings::empty()
        };

        match audio_proxy.set(settings).await? {
            Ok(_) => Ok(to_value(SetUiResult::Success)?),
            Err(e) => Err(format_err!("SetVolume failed with err {:?}", e)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common_utils::test::assert_value_round_trips_as;
    use crate::setui::types::{HourCycle, IntlInfo, LocaleId, TemperatureUnit};
    use fidl::endpoints::create_proxy_and_stream;
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
        let facade = SetUiFacade { audio_proxy: None, display_proxy: Some(proxy) };
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
                            ..DisplaySettings::empty()
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
        let facade = SetUiFacade { audio_proxy: Some(proxy), display_proxy: None };
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
                                ..Volume::empty()
                            }),
                            ..AudioStreamSettings::empty()
                        }
                    );
                    responder.send(&mut Ok(())).unwrap();
                }
                other => panic!("Unexpected stream item: {:?}", other),
            }
        };

        futures::future::join(facade_fut, stream_fut).await;
    }
}
