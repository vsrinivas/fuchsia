// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::display::light_sensor::{open_sensor, read_sensor, Sensor};
use crate::handler::base::{Event, SettingHandlerResult, State};
use crate::handler::setting_handler::{controller, ClientProxy, ControllerError};
use crate::switchboard::base::{ControllerStateResult, LightData, SettingRequest, SettingResponse};
use async_trait::async_trait;
use fidl_fuchsia_input_report::InputDeviceMarker;
use fuchsia_async::{self as fasync, DurationExt};
use fuchsia_zircon::Duration;
use futures::channel::mpsc::{unbounded, UnboundedReceiver};
use futures::future::{AbortHandle, Abortable};
use futures::lock::Mutex;
use futures::prelude::*;
use std::fs::File;
use std::sync::Arc;

pub const LIGHT_SENSOR_SERVICE_NAME: &str = "light_sensor_hid";
pub const LIGHT_SENSOR_CONFIG_PATH: &str = "/config/data/light_sensor_configuration.json";
const SCAN_DURATION_MS: i64 = 1000;

pub struct LightSensorController {
    client: ClientProxy,
    sensor: Sensor,
    current_value: Arc<Mutex<LightData>>,
    notifier_abort: Option<AbortHandle>,
}

#[async_trait]
impl controller::Create for LightSensorController {
    async fn create(client: ClientProxy) -> Result<Self, ControllerError> {
        let service_context = client.get_service_context().await;
        let sensor_proxy_result = service_context
            .lock()
            .await
            .connect_named::<InputDeviceMarker>(LIGHT_SENSOR_SERVICE_NAME)
            .await;

        let sensor = if let Ok(proxy) = sensor_proxy_result {
            Sensor::new(&proxy, &service_context)
                .await
                .map_err(|_| ControllerError::InitFailure("Could not connect to proxy".into()))?
        } else {
            let file = File::open(LIGHT_SENSOR_CONFIG_PATH).map_err(|_| {
                ControllerError::InitFailure("Could not open sensor configuration file".into())
            })?;
            let config = serde_json::from_reader(file).map_err(|_| {
                ControllerError::InitFailure("Could not read sensor configuration file".into())
            })?;
            open_sensor(service_context, config)
                .await
                .map_err(|_| ControllerError::InitFailure("Could not connect to proxy".into()))?
        };

        let current_data = Arc::new(Mutex::new(get_sensor_data(&sensor).await));
        Ok(Self { client, sensor, current_value: current_data, notifier_abort: None })
    }
}

#[async_trait]
impl controller::Handle for LightSensorController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingHandlerResult> {
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
                    start_light_sensor_scanner(self.sensor.clone(), SCAN_DURATION_MS);
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
        Arc::new(Mutex::new(Self { client }))
    }
}

#[async_trait]
impl LightNotifier for ClientNotifier {
    async fn notify(&self) {
        self.client.notify(Event::Changed).await;
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

    fasync::Task::spawn(
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
    )
    .detach();

    abort_handle
}

/// Runs a task that periodically checks for changes on the light sensor
/// and sends notifications when the value changes.
/// Will not send any initial value if nothing changes.
/// This terminates when the receiver closes without panicking.
fn start_light_sensor_scanner(
    sensor: Sensor,
    scan_duration_ms: i64,
) -> UnboundedReceiver<LightData> {
    let (sender, receiver) = unbounded::<LightData>();

    fasync::Task::spawn(async move {
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
    })
    .detach();
    receiver
}

async fn get_sensor_data(sensor: &Sensor) -> LightData {
    let sensor_data = read_sensor(&sensor).await.expect("Could not read from the sensor");
    let lux = sensor_data.illuminance as f32;
    let red = sensor_data.red as f32;
    let green = sensor_data.green as f32;
    let blue = sensor_data.blue as f32;
    LightData { illuminance: lux, color: fidl_fuchsia_ui_types::ColorRgb { red, green, blue } }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::display::light_sensor::testing;
    use crate::service_context::{ExternalServiceProxy, ServiceContext};
    use crate::switchboard::base::SettingType;
    use fidl_fuchsia_input_report::{InputReport, InputReportsReaderReadInputReportsResponder};
    use futures::channel::mpsc::UnboundedSender;
    use std::sync::atomic::{AtomicBool, Ordering};

    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;

    type Notifier = UnboundedSender<SettingType>;

    struct TestNotifier {
        notifier: Notifier,
    }

    impl TestNotifier {
        pub fn create(notifier: Notifier) -> Arc<Mutex<Self>> {
            Arc::new(Mutex::new(Self { notifier }))
        }
    }

    #[async_trait]
    impl LightNotifier for TestNotifier {
        async fn notify(&self) {
            self.notifier.unbounded_send(SettingType::LightSensor).ok();
        }
    }

    struct DataProducer<F>
    where
        F: Fn() -> Vec<InputReport>,
    {
        producer: F,
        turn_on: bool,
    }

    impl<F> DataProducer<F>
    where
        F: Fn() -> Vec<InputReport>,
    {
        fn new(producer: F) -> Self {
            Self { producer, turn_on: false }
        }

        fn trigger(&mut self) {
            self.turn_on = true;
        }

        fn produce(&self) -> Vec<InputReport> {
            let mut data = (self.producer)();
            if self.turn_on {
                // Set illuminance value on second data report to 32
                data[0].sensor.as_mut().unwrap().values.as_mut().unwrap()[1] = 32;
            }

            data
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_auto_brightness_task() {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<InputDeviceMarker>().unwrap();
        let proxy = ExternalServiceProxy::new(proxy, None);

        let (sensor_axes, data_fn) = testing::get_mock_sensor_response();
        let data_producer = Arc::new(Mutex::new(DataProducer::new(data_fn)));
        let data_producer_clone = Arc::clone(&data_producer);
        let data_fn = move || {
            let data_producer = Arc::clone(&data_producer_clone);
            async move { data_producer.lock().await.produce() }
        };

        testing::spawn_mock_sensor_with_data(stream, sensor_axes, data_fn);
        let service_context = ServiceContext::create(None, None);

        let sensor = Sensor::new(&proxy, &service_context).await.unwrap();
        let mut receiver = start_light_sensor_scanner(sensor, 1);

        let sleep_duration = zx::Duration::from_millis(5);
        fasync::Timer::new(sleep_duration.after_now()).await;

        let next = receiver.try_next();
        if let Ok(_) = next {
            panic!("No notifications should happen before value changes")
        };

        data_producer.lock().await.trigger();

        let data = receiver.next().await;
        assert_eq!(data.unwrap().illuminance, 32.0);

        receiver.close();

        // Make sure we don't panic after receiver closes
        let sleep_duration = zx::Duration::from_millis(5);
        fasync::Timer::new(sleep_duration.after_now()).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_light_sensor_scanner_scope() {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<InputDeviceMarker>().unwrap();
        let proxy = ExternalServiceProxy::new(proxy, None);

        let receiver: Arc<Mutex<Option<UnboundedReceiver<LightData>>>> = Arc::new(Mutex::new(None));
        let completed = Arc::new(AtomicBool::new(false));
        let (axes, data_fn) = testing::get_mock_sensor_response();
        let mut counter = 0;
        let receiver_clone = Arc::clone(&receiver);
        let completed_clone = Arc::clone(&completed);
        let data_fn = move |responder: InputReportsReaderReadInputReportsResponder| {
            let mut data = data_fn();

            counter += 1;

            // Close the receiver on the second request (once in the loop)
            let should_close_receiver = counter == 2;

            let receiver = Arc::clone(&receiver_clone);
            let completed = Arc::clone(&completed_clone);
            async move {
                if should_close_receiver {
                    receiver.lock().await.as_mut().unwrap().close();
                }

                // Trigger a change
                data[0].sensor.as_mut().unwrap().values.as_mut().unwrap()[3] += counter;
                responder.send(&mut Ok(data)).unwrap();

                if should_close_receiver {
                    completed.swap(true, Ordering::Relaxed);
                }
            }
        };

        // likely needs to take fn that allows control of when responder sends
        testing::spawn_mock_sensor_with_handler(stream, axes, data_fn);
        let service_context = ServiceContext::create(None, None);

        let sensor = Sensor::new(&proxy, &service_context).await.unwrap();
        *receiver.lock().await = Some(start_light_sensor_scanner(sensor, 1));

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
