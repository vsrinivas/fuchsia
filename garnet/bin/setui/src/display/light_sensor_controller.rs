// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::display::light_sensor::{open_sensor, read_sensor},
    crate::registry::base::{Command, Notifier, State},
    crate::service_context::ServiceContext,
    crate::switchboard::base::{LightData, SettingRequest, SettingResponse, SettingType},
    fidl_fuchsia_hardware_input::{DeviceMarker as SensorMarker, DeviceProxy as SensorProxy},
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon::Duration,
    futures::channel::mpsc::{unbounded, UnboundedReceiver, UnboundedSender},
    futures::future::{AbortHandle, Abortable},
    futures::prelude::*,
    parking_lot::RwLock,
    std::sync::Arc,
};

pub const LIGHT_SENSOR_SERVICE_NAME: &str = "light_sensor_hid";
const SCAN_DURATION_MS: i64 = 1000;

/// Launches a new controller for the light sensor which periodically scans the light sensor
/// for values, and sends out values on change.
pub fn spawn_light_sensor_controller(
    service_context_handle: Arc<RwLock<ServiceContext>>,
) -> UnboundedSender<Command> {
    let (light_sensor_handler_tx, light_sensor_handler_rx) = unbounded::<Command>();

    let notifier_lock = Arc::<RwLock<Option<Notifier>>>::new(RwLock::new(None));

    fasync::spawn(async move {
        // First connect to service if it is provided by service context
        let mut sensor_proxy_result =
            service_context_handle.read().connect_named::<SensorMarker>(LIGHT_SENSOR_SERVICE_NAME);

        // If not, enumuerate through HIDs to try to find it
        if let Err(_) = sensor_proxy_result {
            sensor_proxy_result = open_sensor().await;
        }

        if let Ok(proxy) = sensor_proxy_result {
            let current_value: Arc<RwLock<LightData>> =
                Arc::new(RwLock::new(get_sensor_data(&proxy).await));

            handle_commands(
                proxy,
                light_sensor_handler_rx,
                notifier_lock.clone(),
                current_value.clone(),
            )
            .await;
        } else {
            fx_log_err!("Couldn't connect to light sensor controller");
        }
    });
    light_sensor_handler_tx
}

/// Listens to commands from the registry; doesn't return until stream ends.
async fn handle_commands(
    proxy: SensorProxy,
    mut light_sensor_handler_rx: UnboundedReceiver<Command>,
    notifier: Arc<RwLock<Option<Notifier>>>,
    current_value: Arc<RwLock<LightData>>,
) {
    let mut notifier_abort: Option<AbortHandle> = None;

    while let Some(command) = light_sensor_handler_rx.next().await {
        match command {
            Command::ChangeState(state) => match state {
                State::Listen(new_notifier) => {
                    *notifier.write() = Some(new_notifier);
                    let change_receiver =
                        start_light_sensor_scanner(proxy.clone(), SCAN_DURATION_MS);
                    notifier_abort = Some(
                        notify_on_change(change_receiver, notifier.clone(), current_value.clone())
                            .await,
                    );
                }
                State::EndListen => {
                    if let Some(abort_handle) = notifier_abort {
                        abort_handle.abort();
                    }
                    notifier_abort = None;
                    *notifier.write() = None;
                }
            },
            Command::HandleRequest(request, responder) =>
            {
                #[allow(unreachable_patterns)]
                match request {
                    SettingRequest::Get => {
                        let data = current_value.read().clone();

                        responder.send(Ok(Some(SettingResponse::LightSensor(data)))).unwrap();
                    }
                    _ => panic!("Unexpected command to light sensor"),
                }
            }
        }
    }
}

/// Sends out a notificaiton when value changes. Abortable to allow for
/// startListen and endListen.
async fn notify_on_change(
    mut change_receiver: UnboundedReceiver<LightData>,
    notifier_lock: Arc<RwLock<Option<Notifier>>>,
    current_value: Arc<RwLock<LightData>>,
) -> AbortHandle {
    let (abort_handle, abort_registration) = AbortHandle::new_pair();

    fasync::spawn(
        Abortable::new(
            async move {
                while let Some(value) = change_receiver.next().await {
                    *current_value.write() = value;
                    if let Some(notifier) = (*notifier_lock.read()).clone() {
                        notifier.unbounded_send(SettingType::LightSensor).unwrap();
                    }
                }
            },
            abort_registration,
        )
        .unwrap_or_else(|_| ()),
    );

    abort_handle
}

/// Runs a task that periodically checks for changes on the light sensor
/// and sends notifications when the value changes.
/// Will not send any initial value if nothing changes.
/// This terminates when the receiver closes without panicking.
fn start_light_sensor_scanner(
    sensor: SensorProxy,
    scan_duration_ms: i64,
) -> UnboundedReceiver<LightData> {
    let (sender, receiver) = unbounded::<LightData>();

    fasync::spawn(async move {
        let mut data = get_sensor_data(&sensor).await;

        while !sender.is_closed() {
            let new_data = get_sensor_data(&sensor).await;

            if data != new_data {
                data = new_data;
                sender.unbounded_send(data.clone()).unwrap();
            }

            fasync::Timer::new(Duration::from_millis(scan_duration_ms).after_now()).await;
        }
    });
    receiver
}

async fn get_sensor_data(sensor: &SensorProxy) -> LightData {
    let sensor_data = read_sensor(&sensor).await.expect("Could not read from the sensor");
    let lux: f32 = sensor_data.illuminance.into();
    let red: f32 = sensor_data.red.into();
    let green: f32 = sensor_data.green.into();
    let blue: f32 = sensor_data.blue.into();
    LightData { illuminance: lux, color: fidl_fuchsia_ui_types::ColorRgb { red, green, blue } }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_hardware_input::DeviceRequest as SensorRequest;

    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_auto_brightness_task() {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<SensorMarker>().unwrap();

        let data: Arc<RwLock<[u8; 11]>> =
            Arc::new(RwLock::new([1, 1, 0, 25, 0, 10, 0, 9, 0, 6, 0]));

        let data_clone = data.clone();
        fasync::spawn(async move {
            while let Some(request) = stream.try_next().await.unwrap() {
                if let SensorRequest::GetReport { type_: _, id: _, responder } = request {
                    // Taken from actual sensor report
                    responder.send(0, &mut data_clone.read().iter().cloned()).unwrap();
                }
            }
        });
        let mut receiver = start_light_sensor_scanner(proxy, 1);

        let sleep_duration = zx::Duration::from_millis(5);
        fasync::Timer::new(sleep_duration.after_now()).await;

        let next = receiver.try_next();
        if let Ok(_) = next {
            panic!("No notifications should happen before value changes")
        };

        (*data.write())[3] = 32;

        let data = receiver.next().await;
        assert_eq!(data.unwrap().illuminance, 32.0);

        receiver.close();

        // Make sure we don't panic after receiver closes
        let sleep_duration = zx::Duration::from_millis(5);
        fasync::Timer::new(sleep_duration.after_now()).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_notify_on_change() {
        let data1 = LightData {
            illuminance: 10.0,
            color: fidl_fuchsia_ui_types::ColorRgb { red: 3.0, green: 6.0, blue: 1.0 },
        };
        let data2 = LightData {
            illuminance: 15.0,
            color: fidl_fuchsia_ui_types::ColorRgb { red: 1.0, green: 9.0, blue: 5.0 },
        };

        let (light_sender, light_receiver) = unbounded::<LightData>();

        let (notifier_sender, mut notifier_receiver) = unbounded::<SettingType>();

        let data: Arc<RwLock<LightData>> = Arc::new(RwLock::new(data1));

        let aborter =
            notify_on_change(light_receiver, Arc::new(RwLock::new(Some(notifier_sender))), data)
                .await;

        light_sender.unbounded_send(data2).unwrap();

        assert_eq!(notifier_receiver.next().await.unwrap(), SettingType::LightSensor);

        let next = notifier_receiver.try_next();
        if let Ok(_) = next {
            panic!("Only one change should have happened")
        };

        aborter.abort();

        let sleep_duration = zx::Duration::from_millis(5);
        fasync::Timer::new(sleep_duration.after_now()).await;
        assert_eq!(light_sender.is_closed(), true);
    }
}
