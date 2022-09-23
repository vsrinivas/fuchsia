// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/10064) Remove allow when used.
#![allow(dead_code, unused_variables)]

use crate::input_device::{Handled, InputDeviceDescriptor, InputDeviceEvent, InputEvent};
use crate::input_handler::InputHandler;
use crate::light_sensor::calibrator::{Calibrate, Calibrator};
use crate::light_sensor::led_watcher::{CancelableTask, LedWatcher, LedWatcherHandle};
use crate::light_sensor::types::{Calibration, Rgbc};
use anyhow::{Context, Error};
use async_trait::async_trait;
use async_utils::hanging_get::server::{HangingGet, Publisher};
use fidl_fuchsia_lightsensor::{
    LightSensorData as FidlLightSensorData, Rgbc as FidlRgbc, SensorRequest, SensorRequestStream,
    SensorWatchResponder,
};
use fidl_fuchsia_settings::LightProxy;
use fidl_fuchsia_ui_brightness::ControlProxy as BrightnessControlProxy;
use fuchsia_syslog::fx_log_warn;
use futures::channel::oneshot;
use futures::TryStreamExt;
use std::cell::RefCell;
use std::rc::Rc;

type NotifyFn = Box<dyn Fn(&LightSensorData, SensorWatchResponder) -> bool>;
type SensorHangingGet = HangingGet<LightSensorData, SensorWatchResponder, NotifyFn>;
type SensorPublisher = Publisher<LightSensorData, SensorWatchResponder, NotifyFn>;

#[derive(Clone)]
pub struct LightSensorHandler {
    hanging_get: Rc<RefCell<SensorHangingGet>>,
    calibrator: Calibrator<LedWatcherHandle>,
    watcher_task: Rc<CancelableTask>,
}

impl LightSensorHandler {
    pub async fn new(
        light_proxy: LightProxy,
        brightness_proxy: BrightnessControlProxy,
        calibration: Calibration,
    ) -> Rc<Self> {
        let hanging_get = Rc::new(RefCell::new(HangingGet::new(
            LightSensorData {
                rgbc: Rgbc { red: 0.0, green: 0.0, blue: 0.0, clear: 0.0 },
                calculated_lux: 0.0,
                correlated_color_temperature: 0.0,
            },
            Box::new(|sensor_data: &LightSensorData, responder: SensorWatchResponder| -> bool {
                if let Err(e) = responder.send(FidlLightSensorData::from(*sensor_data)) {
                    fx_log_warn!("Failed to send updated data to client: {e:?}",);
                }
                true
            }) as NotifyFn,
        )));
        let light_groups = vec![];
        let led_watcher = LedWatcher::new(light_groups);
        let (cancelation_tx, cancelation_rx) = oneshot::channel();
        let (led_watcher_handle, watcher_task) = led_watcher
            .handle_light_groups_and_brightness_watch(
                light_proxy,
                brightness_proxy,
                cancelation_rx,
            );
        let watcher_task = Rc::new(CancelableTask::new(cancelation_tx, watcher_task));
        let calibrator = Calibrator::new(calibration, led_watcher_handle);
        Rc::new(Self { hanging_get, calibrator, watcher_task })
    }

    pub async fn complete(self) {
        if let Ok(watcher_task) = Rc::try_unwrap(self.watcher_task) {
            watcher_task.cancel().await;
        }
    }

    pub async fn handle_light_sensor_request_stream(
        self: &Rc<Self>,
        mut stream: SensorRequestStream,
    ) -> Result<(), Error> {
        let subscriber = self.hanging_get.borrow_mut().new_subscriber();
        while let Some(request) =
            stream.try_next().await.context("Error handling light sensor request stream")?
        {
            match request {
                SensorRequest::Watch { responder } => {
                    subscriber
                        .register(responder)
                        .context("registering responder for Watch call")?;
                }
            }
        }

        Ok(())
    }
}

#[async_trait(?Send)]
impl InputHandler for LightSensorHandler {
    async fn handle_input_event(self: Rc<Self>, mut input_event: InputEvent) -> Vec<InputEvent> {
        if let InputEvent {
            device_event: InputDeviceEvent::LightSensor(light_sensor_event),
            device_descriptor: InputDeviceDescriptor::LightSensor(ref light_sensor_descriptor),
            event_time: _,
            handled: Handled::No,
            trace_id: _,
        } = input_event
        {
            let calibrated_rgbc =
                self.calibrator.calibrate(light_sensor_event.rgbc.map(|c| c as f32));
            // TODO(fxbug.dev/100664) implement calculations.
            let publisher = self.hanging_get.borrow().new_publisher();
            publisher.set(LightSensorData {
                rgbc: calibrated_rgbc,
                calculated_lux: 0.0,
                correlated_color_temperature: 0.0,
            });
            input_event.handled = Handled::Yes;
        }
        vec![input_event]
    }
}

#[derive(Copy, Clone, PartialEq)]
struct LightSensorData {
    rgbc: Rgbc<f32>,
    calculated_lux: f32,
    correlated_color_temperature: f32,
}

impl From<LightSensorData> for FidlLightSensorData {
    fn from(data: LightSensorData) -> Self {
        Self {
            rgbc: Some(FidlRgbc::from(data.rgbc)),
            calculated_lux: Some(data.calculated_lux),
            correlated_color_temperature: Some(data.correlated_color_temperature),
            ..FidlLightSensorData::EMPTY
        }
    }
}

impl From<Rgbc<f32>> for FidlRgbc {
    fn from(rgbc: Rgbc<f32>) -> Self {
        Self {
            red_intensity: rgbc.red,
            green_intensity: rgbc.green,
            blue_intensity: rgbc.blue,
            clear_intensity: rgbc.clear,
        }
    }
}
