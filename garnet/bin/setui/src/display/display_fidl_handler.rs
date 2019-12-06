// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::switchboard::base::*,
    crate::switchboard::hanging_get_handler::{HangingGetHandler, Sender},
    crate::switchboard::switchboard_impl::SwitchboardImpl,
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync,
    futures::lock::Mutex,
    futures::prelude::*,
    parking_lot::RwLock,
    std::sync::Arc,
};

impl Sender<DisplaySettings> for DisplayWatchResponder {
    fn send_response(self, data: DisplaySettings) {
        self.send(&mut Ok(data)).log_fidl_response_error(DisplayMarker::DEBUG_NAME);
    }
}

impl Sender<LightSensorData> for DisplayWatchLightSensorResponder {
    fn send_response(self, data: LightSensorData) {
        self.send(&mut Ok(data)).log_fidl_response_error(DisplayMarker::DEBUG_NAME);
    }
}

impl From<SettingResponse> for LightSensorData {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::LightSensor(data) = response {
            let mut sensor_data = fidl_fuchsia_settings::LightSensorData::empty();
            sensor_data.illuminance_lux = Some(data.illuminance);
            sensor_data.color = Some(data.color);
            sensor_data
        } else {
            panic!("incorrect value sent to display");
        }
    }
}

impl From<SettingResponse> for DisplaySettings {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::Brightness(info) = response {
            let mut display_settings = fidl_fuchsia_settings::DisplaySettings::empty();

            display_settings.auto_brightness = Some(info.auto_brightness);

            if !info.auto_brightness {
                display_settings.brightness_value = Some(info.manual_brightness_value);
            }

            display_settings
        } else {
            panic!("incorrect value sent to display");
        }
    }
}

fn to_request(settings: DisplaySettings) -> Option<SettingRequest> {
    let mut request = None;
    if let Some(brightness_value) = settings.brightness_value {
        request = Some(SettingRequest::SetBrightness(brightness_value));
    } else if let Some(enable_auto_brightness) = settings.auto_brightness {
        request = Some(SettingRequest::SetAutoBrightness(enable_auto_brightness));
    }
    request
}

pub fn spawn_display_fidl_handler(
    switchboard_handle: Arc<RwLock<SwitchboardImpl>>,
    mut stream: DisplayRequestStream,
) {
    let switchboard_lock = switchboard_handle.clone();

    let display_hanging_get_handler: Arc<
        Mutex<HangingGetHandler<DisplaySettings, DisplayWatchResponder>>,
    > = HangingGetHandler::create(switchboard_handle.clone(), SettingType::Display);

    let light_sensor_hanging_get_handler: Arc<
        Mutex<HangingGetHandler<LightSensorData, DisplayWatchLightSensorResponder>>,
    > = HangingGetHandler::create(switchboard_handle.clone(), SettingType::LightSensor);

    fasync::spawn(async move {
        while let Ok(Some(req)) = stream.try_next().await {
            // Support future expansion of FIDL
            #[allow(unreachable_patterns)]
            match req {
                DisplayRequest::Set { settings, responder } => {
                    if let Some(request) = to_request(settings) {
                        let (response_tx, _response_rx) =
                            futures::channel::oneshot::channel::<SettingResponseResult>();
                        let mut switchboard = switchboard_lock.write();
                        let result =
                            switchboard.request(SettingType::Display, request, response_tx);

                        match result {
                            Ok(_) => responder
                                .send(&mut Ok(()))
                                .log_fidl_response_error(DisplayMarker::DEBUG_NAME),
                            Err(_err) => responder
                                .send(&mut Err(Error::Unsupported))
                                .log_fidl_response_error(DisplayMarker::DEBUG_NAME),
                        }
                    } else {
                        responder
                            .send(&mut Err(Error::Unsupported))
                            .log_fidl_response_error(DisplayMarker::DEBUG_NAME);
                    }
                }
                DisplayRequest::Watch { responder } => {
                    let mut hanging_get_lock = display_hanging_get_handler.lock().await;
                    hanging_get_lock.watch(responder).await;
                }
                DisplayRequest::WatchLightSensor { delta, responder } => {
                    let mut hanging_get_lock = light_sensor_hanging_get_handler.lock().await;
                    hanging_get_lock
                        .watch_with_change_fn(
                            Box::new(
                                move |old_data: &LightSensorData, new_data: &LightSensorData| {
                                    if let (Some(old_lux), Some(new_lux)) =
                                        (old_data.illuminance_lux, new_data.illuminance_lux)
                                    {
                                        (new_lux - old_lux).abs() >= delta
                                    } else {
                                        true
                                    }
                                },
                            ),
                            responder,
                        )
                        .await;
                }
                _ => {}
            }
        }
    });
}
