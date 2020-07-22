// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::display::light_sensor::{open_sensor, read_sensor};
use crate::registry::base::State;
use crate::registry::setting_handler::{controller, ClientProxy, ControllerError};
use crate::switchboard::base::{
    ControllerStateResult, LightData, SettingRequest, SettingResponse, SettingResponseResult,
};
use async_trait::async_trait;
use fidl_fuchsia_hardware_input::{DeviceMarker as SensorMarker, DeviceProxy as SensorProxy};
use fuchsia_async::{self as fasync, DurationExt};
use fuchsia_zircon::Duration;
use futures::channel::mpsc::{unbounded, UnboundedReceiver};
use futures::future::{AbortHandle, Abortable};
use futures::lock::Mutex;
use futures::prelude::*;
use std::sync::Arc;

pub const LIGHT_SENSOR_SERVICE_NAME: &str = "light_sensor_hid";
const SCAN_DURATION_MS: i64 = 1000;

pub struct LightSensorController {
    client: ClientProxy,
    proxy: SensorProxy,
    current_value: Arc<Mutex<LightData>>,
    notifier_abort: Option<AbortHandle>,
}

#[async_trait]
impl controller::Create for LightSensorController {
    async fn create(client: ClientProxy) -> Result<Self, ControllerError> {
        let service_context = client.get_service_context().await;
        let mut sensor_proxy_result = service_context
            .lock()
            .await
            .connect_named::<SensorMarker>(LIGHT_SENSOR_SERVICE_NAME)
            .await;

        if sensor_proxy_result.is_err() {
            sensor_proxy_result = open_sensor().await;
        }

        if let Ok(proxy) = sensor_proxy_result {
            let current_data = Arc::new(Mutex::new(get_sensor_data(&proxy).await));
            Ok(Self {
                client: client,
                proxy: proxy,
                current_value: current_data,
                notifier_abort: None,
            })
        } else {
            Err(ControllerError::InitFailure {
                description: "Could not connect to proxy".to_string(),
            })
        }
    }
}

#[async_trait]
impl controller::Handle for LightSensorController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingResponseResult> {
        #[allow(unreachable_patterns)]
        match request {
            SettingRequest::Get => Some(Ok(Some(SettingResponse::LightSensor(
                self.current_value.lock().await.clone(),
            )))),
            _ => None,
        }
    }

    async fn change_state(&mut self, state: State) -> Option<ControllerStateResult> {
        match state {
            State::Listen => {
                let change_receiver =
                    start_light_sensor_scanner(self.proxy.clone(), SCAN_DURATION_MS);
                self.notifier_abort = Some(
                    notify_on_change(
                        change_receiver,
                        ClientNotifier::create(self.client.clone()),
                        self.current_value.clone(),
                    )
                    .await,
                );
            }
            State::EndListen => {
                if let Some(abort_handle) = &self.notifier_abort {
                    abort_handle.abort();
                }
                self.notifier_abort = None;
            }
            _ => {}
        };
        None
    }
}

#[async_trait]
trait LightNotifier {
    async fn notify(&self);
}

struct ClientNotifier {
    client: ClientProxy,
}

impl ClientNotifier {
    pub fn create(client: ClientProxy) -> Arc<Mutex<Self>> {
        Arc::new(Mutex::new(Self { client: client }))
    }
}

#[async_trait]
impl LightNotifier for ClientNotifier {
    async fn notify(&self) {
        self.client.notify().await;
    }
}

/// Sends out a notificaiton when value changes. Abortable to allow for
/// startListen and endListen.
async fn notify_on_change(
    mut change_receiver: UnboundedReceiver<LightData>,
    notifier: Arc<Mutex<dyn LightNotifier + Send + Sync>>,
    current_value: Arc<Mutex<LightData>>,
) -> AbortHandle {
    let (abort_handle, abort_registration) = AbortHandle::new_pair();

    fasync::spawn(
        Abortable::new(
            async move {
                while let Some(value) = change_receiver.next().await {
                    *current_value.lock().await = value;
                    notifier.lock().await.notify().await;
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
                if sender.unbounded_send(data.clone()).is_err() {
                    break;
                }
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
    use crate::switchboard::base::SettingType;
    use fidl_fuchsia_hardware_input::DeviceRequest as SensorRequest;
    use futures::channel::mpsc::UnboundedSender;
    use std::sync::atomic::{AtomicBool, Ordering};

    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use parking_lot::RwLock;

    type Notifier = UnboundedSender<SettingType>;

    struct TestNotifier {
        notifier: Notifier,
    }

    impl TestNotifier {
        pub fn create(notifier: Notifier) -> Arc<Mutex<Self>> {
            Arc::new(Mutex::new(Self { notifier: notifier }))
        }
    }

    #[async_trait]
    impl LightNotifier for TestNotifier {
        async fn notify(&self) {
            self.notifier.unbounded_send(SettingType::LightSensor).ok();
        }
    }

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
                    responder.send(0, &*data_clone.read()).unwrap();
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
    async fn test_start_light_sensor_scanner_scope() {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<SensorMarker>().unwrap();
        let mut receiver = start_light_sensor_scanner(proxy, 1);

        let completed = Arc::new(AtomicBool::new(false));
        let completed_clone = completed.clone();
        fasync::spawn(async move {
            let mut counter: u8 = 0;
            let mut data = [1, 1, 0, 25, 0, 10, 0, 9, 0, 6, 0];
            while let Some(request) = stream.try_next().await.unwrap() {
                if let SensorRequest::GetReport { type_: _, id: _, responder } = request {
                    // Taken from actual sensor report
                    counter += 1;

                    // Close the receiver on the second request (once in the
                    // loop)
                    let should_close_receiver = counter == 2;

                    if should_close_receiver {
                        receiver.close();
                    }

                    // Trigger a change.
                    {
                        data[3] += 1;
                    }
                    responder.send(0, &data).unwrap();

                    if should_close_receiver {
                        completed_clone.swap(true, Ordering::Relaxed);
                    }
                }
            }
        });

        fasync::Timer::new(zx::Duration::from_millis(5).after_now()).await;
        // Allow multiple iterations
        assert!(completed.load(Ordering::Relaxed));
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

        let data: Arc<Mutex<LightData>> = Arc::new(Mutex::new(data1));

        let aborter =
            notify_on_change(light_receiver, TestNotifier::create(notifier_sender), data).await;

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
