// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::fidl_processor::FidlProcessor, crate::switchboard::base::*,
    crate::switchboard::hanging_get_handler::Sender,
    crate::switchboard::switchboard_impl::SwitchboardImpl, fidl::endpoints::ServiceMarker,
    fidl_fuchsia_settings::*, fuchsia_async as fasync, futures::future::LocalBoxFuture,
    futures::prelude::*, parking_lot::RwLock, std::sync::Arc,
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
    stream: DisplayRequestStream,
) {
    fasync::spawn_local(async move {
        let mut processor = FidlProcessor::<DisplayMarker>::new(stream, switchboard_handle.clone());
        processor.register::<DisplaySettings, DisplayWatchResponder>(
            SettingType::Display,
            Box::new(move |context, req| -> LocalBoxFuture<'_, Result<Option<DisplayRequest>, failure::Error>> {
                async move {
                    // Support future expansion of FIDL
                    #[allow(unreachable_patterns)]
                    match req {
                         DisplayRequest::Set { settings, responder } => {
                             if let Some(request) = to_request(settings) {
                                let (response_tx, _response_rx) =
                                    futures::channel::oneshot::channel::<SettingResponseResult>();
                                let result =
                                    context.switchboard.write().request(SettingType::Display, request, response_tx);

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
                            }},
                        DisplayRequest::Watch { responder } => {
                            context.watch(responder).await;
                        },
                        _ => {
                            return Ok(Some(req));
                        }
                    }

                    return Ok(None);
                }
                .boxed_local()
            }),
        );

        processor.register::<LightSensorData, DisplayWatchLightSensorResponder>(
            SettingType::LightSensor,
            Box::new(move |context, req| -> LocalBoxFuture<'_, Result<Option<DisplayRequest>, failure::Error>> {
                async move {
                    // Support future expansion of FIDL
                    #[allow(unreachable_patterns)]
                    match req {
                        DisplayRequest::WatchLightSensor { delta, responder } => {
                            //let mut hanging_get_lock = light_sensor_hanging_get_handler.lock().await;
                            context
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
                        _ => {
                            return Ok(Some(req));
                        }
                    }

                    return Ok(None);
                }
                .boxed_local()
            }),
        );
        processor.process().await;
    });
}
