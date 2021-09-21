// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;
use std::{fs, io};

use anyhow::{format_err, Error};
use async_trait::async_trait;
use fidl_fuchsia_ui_brightness::{
    ControlRequest as BrightnessControlRequest, ControlWatchAutoBrightnessAdjustmentResponder,
    ControlWatchAutoBrightnessResponder, ControlWatchCurrentBrightnessResponder,
};
use fuchsia_async::{self as fasync, DurationExt};
use fuchsia_syslog::{self, fx_log_err, fx_log_info, fx_log_warn};
use fuchsia_zircon::sys::ZX_ERR_NOT_SUPPORTED;
use fuchsia_zircon::{Duration, DurationNum};
use futures::channel::mpsc::UnboundedSender;
use futures::future::{AbortHandle, Abortable};
use futures::lock::Mutex;
use futures::prelude::*;
use lazy_static::lazy_static;
use serde::{Deserialize, Serialize};
use serde_json;
use splines::{Interpolation, Key, Spline};
use watch_handler::{Sender, WatchHandler};

use crate::backlight::BacklightControl;
use crate::sender_channel::SenderChannel;
use crate::sensor::SensorControl;

// Delay between sensor reads
const SLOW_SCAN_TIMEOUT_MS: i64 = 2000;
// Delay if we have made a large change in auto brightness
const QUICK_SCAN_TIMEOUT_MS: i64 = 100;
// What constitutes a large change in brightness?
// This seems small but it is significant and works nicely.
const LARGE_CHANGE_THRESHOLD_NITS: i32 = 0.016 as i32;
const AUTO_MINIMUM_BRIGHTNESS: f32 = 0.004;
const BRIGHTNESS_USER_MULTIPLIER_CENTER: f32 = 1.0;
const BRIGHTNESS_USER_MULTIPLIER_MAX: f32 = 8.0;
const BRIGHTNESS_USER_MULTIPLIER_MIN: f32 = 0.25;
const BRIGHTNESS_TABLE_FILE_PATH: &str = "/data/brightness_table";
const BRIGHTNESS_MINIMUM_CHANGE: f32 = 0.00001;
const MANUAL_BRIGHTNESS_DEFAULT: Duration = Duration::from_millis(250);

// Minimum time between brightness steps. 50 ms is the configured ramp time of the backlight.
// It'll do its smoothing within this, so there is no need to send events more frequently.
const BRIGHTNESS_MIN_STEP_TIME_MS: f64 = 50_f64;
// Brightness changes should take this time unless we exceed the MAX_BRIGHTNESS_STEP_TIME
const BRIGHTNESS_CHANGE_DURATION_MS: i64 = 2000;
// The number of possible values for the backlight hardware (0 - 4095)
// This is used to ensure we are not trying to set 1.0, 1.2, 1.4, 1.6, 1.8, 2.0
// Which would actually set 1, 1, 1, 2, 2, 2
// Instead we set 1 and 2 over a longer period of time.
const BACKLIGHT_GRANULARITY: f64 = 4095_f64;

#[derive(Serialize, Deserialize, Clone)]
struct BrightnessPoint {
    ambient_lux: f32,
    display_nits: f32,
}

#[derive(Serialize, Deserialize, Clone)]
struct BrightnessTable {
    points: Vec<BrightnessPoint>,
}

impl From<fidl_fuchsia_ui_brightness::BrightnessPoint> for BrightnessPoint {
    fn from(brightness_point: fidl_fuchsia_ui_brightness::BrightnessPoint) -> Self {
        return BrightnessPoint {
            ambient_lux: brightness_point.ambient_lux,
            display_nits: brightness_point.display_nits,
        };
    }
}

impl From<fidl_fuchsia_ui_brightness::BrightnessTable> for BrightnessTable {
    fn from(brightness_table: fidl_fuchsia_ui_brightness::BrightnessTable) -> Self {
        let fidl_fuchsia_ui_brightness::BrightnessTable { points } = brightness_table;
        return BrightnessTable { points: points.into_iter().map(|p| p.into()).collect() };
    }
}

//This is the default table, and a default curve will be generated base on this table.
//This will be replaced once SetBrightnessTable is called.
lazy_static! {
    static ref BRIGHTNESS_TABLE: Arc<Mutex<BrightnessTable>> = {
        let mut lux_to_nits = Vec::new();
        lux_to_nits.push(BrightnessPoint { ambient_lux: 0., display_nits: 0. });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 10., display_nits: 3.33 });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 30., display_nits: 8.7 });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 60., display_nits: 18.27 });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 100., display_nits: 32.785 });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 150., display_nits: 36.82 });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 210., display_nits: 75.0 });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 250., display_nits: 124.16 });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 300., display_nits: 162.96 });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 340., display_nits: 300. });
        Arc::new(Mutex::new(BrightnessTable { points: lux_to_nits }))
    };
}

lazy_static! {
    static ref AUTO_BRIGHTNESS_ADJUSTMENT: Arc<Mutex<f32>> = Arc::new(Mutex::new(1.0));
    static ref GET_BRIGHTNESS_FAILED_FIRST: Arc<Mutex<bool>> = Arc::new(Mutex::new(true));
    static ref LAST_SET_BRIGHTNESS: Arc<Mutex<f32>> = Arc::new(Mutex::new(1.0));
    static ref BRIGHTNESS_CHANGE_DURATION: Arc<Mutex<Duration>> =
        Arc::new(Mutex::new(BRIGHTNESS_CHANGE_DURATION_MS.millis()));
}

pub struct WatcherCurrentResponder {
    watcher_current_responder: ControlWatchCurrentBrightnessResponder,
}

impl Sender<f32> for WatcherCurrentResponder {
    fn send_response(self, data: f32) {
        if let Err(e) = self.watcher_current_responder.send(data) {
            fx_log_err!("Failed to reply to WatchCurrentBrightness: {}", e);
        }
    }
}

pub struct WatcherAutoResponder {
    watcher_auto_responder: ControlWatchAutoBrightnessResponder,
}

impl Sender<bool> for WatcherAutoResponder {
    fn send_response(self, data: bool) {
        if let Err(e) = self.watcher_auto_responder.send(data) {
            fx_log_err!("Failed to reply to WatchAutoBrightness: {}", e);
        }
    }
}

pub struct WatcherAdjustmentResponder {
    watcher_adjustment_responder: ControlWatchAutoBrightnessAdjustmentResponder,
}

impl Sender<f32> for WatcherAdjustmentResponder {
    fn send_response(self, data: f32) {
        if let Err(e) = self.watcher_adjustment_responder.send(data) {
            fx_log_err!("Failed to reply to WatchAutoBrightnessAdjustment: {}", e);
        }
    }
}

#[derive(Debug)]
pub struct Control {
    sensor: Arc<Mutex<dyn SensorControl>>,
    backlight: Arc<Mutex<dyn BacklightControl>>,
    set_brightness_abort_handle: Arc<Mutex<Option<AbortHandle>>>,
    auto_brightness_abort_handle: Option<AbortHandle>,
    spline: Spline<f32, f32>,
    current_sender_channel: Arc<Mutex<SenderChannel<f32>>>,
    auto_sender_channel: Arc<Mutex<SenderChannel<bool>>>,
    adjustment_sender_channel: Arc<Mutex<SenderChannel<f32>>>,
}

impl Control {
    pub async fn new(
        sensor: Arc<Mutex<dyn SensorControl>>,
        backlight: Arc<Mutex<dyn BacklightControl>>,
        current_sender_channel: Arc<Mutex<SenderChannel<f32>>>,
        auto_sender_channel: Arc<Mutex<SenderChannel<bool>>>,
        adjustment_sender_channel: Arc<Mutex<SenderChannel<f32>>>,
    ) -> Control {
        fx_log_info!("New Control class");

        let set_brightness_abort_handle = Arc::new(Mutex::new(None::<AbortHandle>));
        let default_table_points = &*BRIGHTNESS_TABLE.lock().await.points;
        let brightness_table = read_brightness_table_file(BRIGHTNESS_TABLE_FILE_PATH)
            .unwrap_or_else(|e| {
                fx_log_warn!(
                    "Failed to read existing settings: {}, using default table instead.",
                    e
                );
                BrightnessTable { points: default_table_points.to_vec() }
            });

        let spline = generate_spline(&brightness_table);

        let auto_brightness_abort_handle = None::<AbortHandle>;
        let mut result = Control {
            backlight,
            sensor,
            set_brightness_abort_handle,
            auto_brightness_abort_handle,
            spline,
            current_sender_channel,
            auto_sender_channel,
            adjustment_sender_channel,
        };
        // Startup auto-brightness loop
        result.start_auto_brightness_task();
        result
    }

    pub async fn handle_request(
        &mut self,
        request: BrightnessControlRequest,
        watch_current_handler: Arc<Mutex<WatchHandler<f32, WatcherCurrentResponder>>>,
        watch_auto_handler: Arc<Mutex<WatchHandler<bool, WatcherAutoResponder>>>,
        watch_adjustment_handler: Arc<Mutex<WatchHandler<f32, WatcherAdjustmentResponder>>>,
    ) {
        // TODO(kpt): "Consider adding additional tests against the resulting FIDL service itself so
        // that you can ensure it continues serving clients correctly."
        match request {
            BrightnessControlRequest::SetAutoBrightness { control_handle: _ } => {
                self.set_auto_brightness().await;
            }
            BrightnessControlRequest::WatchAutoBrightness { responder } => {
                let watch_auto_result =
                    self.watch_auto_brightness(watch_auto_handler, responder).await;
                match watch_auto_result {
                    Ok(_v) => {}
                    Err(e) => fx_log_err!("Watch auto brightness failed due to err {}.", e),
                }
            }
            BrightnessControlRequest::SetManualBrightness { value, control_handle: _ } => {
                let value = num_traits::clamp(value, 0.0, 1.0);
                self.set_manual_brightness(value).await;
            }
            BrightnessControlRequest::SetManualBrightnessSmooth {
                value,
                duration: duration_ns,
                control_handle: _,
            } => {
                self.set_manual_brightness_smooth(value, Duration::from_nanos(duration_ns)).await;
            }
            BrightnessControlRequest::WatchCurrentBrightness { responder } => {
                let watch_current_result =
                    self.watch_current_brightness(watch_current_handler, responder).await;
                match watch_current_result {
                    Ok(_v) => {}
                    Err(e) => fx_log_err!("Watch current brightness failed due to err {}.", e),
                }
            }
            BrightnessControlRequest::SetBrightnessTable { table, control_handle: _ } => {
                let result = self.check_brightness_table_and_set_new_curve(&table.into()).await;
                match result {
                    Ok(_v) => fx_log_info!("Brightness table is valid and set"),
                    Err(e) => {
                        // TODO(lingxueluo): Close the connection if brightness table not valid.
                        fx_log_err!("Brightness table is not valid because {}", e);
                    }
                }
            }

            BrightnessControlRequest::SetAutoBrightnessAdjustment {
                adjustment,
                control_handle: _,
            } => {
                self.scale_new_adjustment(adjustment).await;
            }
            BrightnessControlRequest::WatchAutoBrightnessAdjustment { responder } => {
                let watch_adjustment_result = self
                    .watch_auto_brightness_adjustment(watch_adjustment_handler, responder)
                    .await;
                match watch_adjustment_result {
                    Ok(_v) => {}
                    Err(e) => fx_log_err!("Watch adjustment failed due to err {}.", e),
                }
            }
            BrightnessControlRequest::GetMaxAbsoluteBrightness { responder } => {
                let result = self.get_max_absolute_brightness();
                match result.await {
                    Ok(value) => {
                        if let Err(e) = responder.send(&mut Ok(value)) {
                            fx_log_err!("Failed to reply to GetMaxAbsoluteBrightness: {}", e);
                        }
                    }
                    Err(e) => {
                        fx_log_err!("Failed to get max absolute brightness: {}", e);

                        if let Err(e) = responder.send(&mut Err(ZX_ERR_NOT_SUPPORTED)) {
                            fx_log_err!("Failed to reply to GetMaxAbsoluteBrightness: {}", e);
                        }
                    }
                }
            }
        }
    }

    pub async fn add_current_sender_channel(&mut self, sender: UnboundedSender<f32>) {
        self.current_sender_channel.lock().await.add_sender_channel(sender).await;
    }

    pub async fn add_auto_sender_channel(&mut self, sender: UnboundedSender<bool>) {
        self.auto_sender_channel.lock().await.add_sender_channel(sender).await;
    }

    pub async fn add_adjustment_sender_channel(&mut self, sender: UnboundedSender<f32>) {
        self.adjustment_sender_channel.lock().await.add_sender_channel(sender).await;
    }

    pub fn get_backlight_and_auto_brightness_on(
        &mut self,
    ) -> (Arc<Mutex<dyn BacklightControl>>, bool) {
        (self.backlight.clone(), self.auto_brightness_abort_handle.is_some())
    }

    // FIDL message handlers
    async fn set_auto_brightness(&mut self) {
        if self.auto_brightness_abort_handle.is_none() {
            fx_log_info!("Auto-brightness turned on");
            self.start_auto_brightness_task();
            self.auto_sender_channel
                .lock()
                .await
                .send_value(self.auto_brightness_abort_handle.is_some());
        }
    }

    async fn watch_auto_brightness(
        &mut self,
        watch_auto_handler: Arc<Mutex<WatchHandler<bool, WatcherAutoResponder>>>,
        responder: ControlWatchAutoBrightnessResponder,
    ) -> Result<(), Error> {
        let mut hanging_get_lock = watch_auto_handler.lock().await;
        hanging_get_lock.watch(WatcherAutoResponder { watcher_auto_responder: responder })?;
        Ok(())
    }

    fn start_auto_brightness_task(&mut self) {
        if let Some(handle) = &self.auto_brightness_abort_handle.take() {
            handle.abort();
        }
        let backlight = self.backlight.clone();
        let sensor = self.sensor.clone();
        let set_brightness_abort_handle = self.set_brightness_abort_handle.clone();
        let current_sender_channel = self.current_sender_channel.clone();
        let spline = self.spline.clone();
        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        fasync::Task::spawn(
            Abortable::new(
                async move {
                    let backlight = backlight.clone();
                    let max_brightness = {
                        let backlight = backlight.clone();
                        let backlight = backlight.lock().await;
                        let max_result = backlight.get_max_absolute_brightness();
                        match max_result.await {
                            Ok(max_value) => max_value,
                            Err(_e) => 250.0,
                        }
                    };
                    // initialize to an impossible number
                    let mut last_value: i32 = -1;
                    loop {
                        let current_sender_channel = current_sender_channel.clone();
                        let sensor = sensor.clone();
                        let mut value =
                            read_sensor_and_get_brightness(sensor, &spline, max_brightness).await;
                        let adjustment = *AUTO_BRIGHTNESS_ADJUSTMENT.lock().await;
                        value = num_traits::clamp(value * adjustment, AUTO_MINIMUM_BRIGHTNESS, 1.0);
                        set_brightness(
                            value,
                            set_brightness_abort_handle.clone(),
                            backlight.clone(),
                            current_sender_channel,
                        )
                        .await;
                        let large_change =
                            (last_value as i32 - value as i32).abs() > LARGE_CHANGE_THRESHOLD_NITS;
                        last_value = value as i32;
                        let delay_timeout =
                            if large_change { QUICK_SCAN_TIMEOUT_MS } else { SLOW_SCAN_TIMEOUT_MS };
                        fuchsia_async::Timer::new(Duration::from_millis(delay_timeout).after_now())
                            .await;
                    }
                },
                abort_registration,
            )
            .unwrap_or_else(|_| ()),
        )
        .detach();
        self.auto_brightness_abort_handle = Some(abort_handle);
    }

    async fn set_manual_brightness(&mut self, value: f32) {
        self.set_manual_brightness_smooth(value, MANUAL_BRIGHTNESS_DEFAULT).await;
    }

    async fn set_manual_brightness_smooth(&mut self, value: f32, duration: Duration) {
        if let Some(handle) = self.auto_brightness_abort_handle.take() {
            fx_log_info!("Auto-brightness off, brightness set to {}", value);
            handle.abort();
        }

        let value = num_traits::clamp(value, 0.0, 1.0);
        {
            // Hold the locks for as little time as possible
            *BRIGHTNESS_CHANGE_DURATION.lock().await = duration;
            self.auto_sender_channel.lock().await.send_value(false);
        }
        let current_sender_channel = self.current_sender_channel.clone();
        set_brightness(
            value,
            self.set_brightness_abort_handle.clone(),
            self.backlight.clone(),
            current_sender_channel,
        )
        .await;
    }

    async fn watch_current_brightness(
        &mut self,
        watch_current_handler: Arc<Mutex<WatchHandler<f32, WatcherCurrentResponder>>>,
        responder: ControlWatchCurrentBrightnessResponder,
    ) -> Result<(), Error> {
        let mut hanging_get_lock = watch_current_handler.lock().await;
        hanging_get_lock.watch(WatcherCurrentResponder { watcher_current_responder: responder })?;
        Ok(())
    }

    async fn set_brightness_curve(&mut self, table: &BrightnessTable) {
        fx_log_info!("Setting new brightness curve.");
        self.spline = generate_spline(table);

        self.start_auto_brightness_task();

        let result = self.store_brightness_table(table, BRIGHTNESS_TABLE_FILE_PATH);
        match result {
            Ok(_v) => fx_log_info!("Stored successfully"),
            Err(e) => fx_log_info!("Didn't store successfully due to error {}", e),
        }
    }

    fn store_brightness_table(
        &mut self,
        table: &BrightnessTable,
        file_path: &str,
    ) -> Result<(), Error> {
        fx_log_info!("Storing brightness table set.");
        let file = fs::File::create(file_path)?;
        serde_json::to_writer(io::BufWriter::new(file), &table)
            .map_err(|e| anyhow::format_err!("Failed to write to file, ran into error: {:?}", e))
    }

    async fn check_brightness_table_and_set_new_curve(
        &mut self,
        table: &BrightnessTable,
    ) -> Result<(), Error> {
        let BrightnessTable { points } = table;
        if points.is_empty() {
            fx_log_info!("Brightness table can not be empty, use the default table instead.");
            let BrightnessTable { points } = &*BRIGHTNESS_TABLE.lock().await;
            let brightness_table = BrightnessTable { points: points.to_vec() };
            self.set_brightness_curve(&brightness_table).await;
            return Ok(());
        }
        let mut last_lux = -1.0;
        for brightness_point in points {
            if brightness_point.ambient_lux < 0.0 || brightness_point.display_nits < 0.0 {
                fx_log_info!("Lux or nits in this table is negative.");
                return Err(format_err!(format!("Lux or nits in this table is negative.")));
            }
            if brightness_point.ambient_lux > last_lux {
                last_lux = brightness_point.ambient_lux;
            } else {
                fx_log_info!("Not increasing lux in this table.");
                return Err(format_err!(format!("Not increasing lux in this table.")));
            }
        }
        self.set_brightness_curve(&table).await;
        Ok(())
    }

    async fn scale_new_adjustment(&mut self, mut adjustment: f32) -> f32 {
        self.adjustment_sender_channel.lock().await.send_value(adjustment);
        // |adjustment| ranges from [-1.0, 1.0]
        // Default adjustment is 0.0, map that to x1.0
        if adjustment >= 0.0 {
            // Map from [0.0, 1.0] to [BRIGHTNESS_USER_MULTIPLIER_CENTER,
            // BRIGHTNESS_USER_MULTIPLIER_MAX]
            adjustment = adjustment
                * (BRIGHTNESS_USER_MULTIPLIER_MAX - BRIGHTNESS_USER_MULTIPLIER_CENTER)
                + BRIGHTNESS_USER_MULTIPLIER_CENTER;
        } else {
            // Map from [-1.0, 0.0) to [BRIGHTNESS_USER_MULTIPLIER_MIN,
            // BRIGHTNESS_USER_MULTIPLIER_CENTER)
            adjustment = adjustment
                * (BRIGHTNESS_USER_MULTIPLIER_CENTER - BRIGHTNESS_USER_MULTIPLIER_MIN)
                + BRIGHTNESS_USER_MULTIPLIER_CENTER;
        }

        *AUTO_BRIGHTNESS_ADJUSTMENT.lock().await = adjustment;
        return adjustment;
    }

    async fn watch_auto_brightness_adjustment(
        &mut self,
        watch_adjustment_handler: Arc<Mutex<WatchHandler<f32, WatcherAdjustmentResponder>>>,
        responder: ControlWatchAutoBrightnessAdjustmentResponder,
    ) -> Result<(), Error> {
        let mut hanging_get_lock = watch_adjustment_handler.lock().await;
        hanging_get_lock
            .watch(WatcherAdjustmentResponder { watcher_adjustment_responder: responder })?;
        Ok(())
    }

    async fn get_max_absolute_brightness(&mut self) -> Result<f64, Error> {
        let backlight = self.backlight.lock().await;
        backlight.get_max_absolute_brightness().await
    }
}

#[async_trait(? Send)]
pub trait ControlTrait {
    async fn handle_request(
        &mut self,
        request: BrightnessControlRequest,
        watch_current_handler: Arc<Mutex<WatchHandler<f32, WatcherCurrentResponder>>>,
        watch_auto_handler: Arc<Mutex<WatchHandler<bool, WatcherAutoResponder>>>,
        watch_adjustment_handler: Arc<Mutex<WatchHandler<f32, WatcherAdjustmentResponder>>>,
    );
    async fn add_current_sender_channel(&mut self, sender: UnboundedSender<f32>);
    async fn add_auto_sender_channel(&mut self, sender: UnboundedSender<bool>);
    async fn add_adjustment_sender_channel(&mut self, sender: UnboundedSender<f32>);
    fn get_backlight_and_auto_brightness_on(&mut self) -> (Arc<Mutex<dyn BacklightControl>>, bool);
}

#[async_trait(? Send)]
impl ControlTrait for Control {
    async fn handle_request(
        &mut self,
        request: BrightnessControlRequest,
        watch_current_handler: Arc<Mutex<WatchHandler<f32, WatcherCurrentResponder>>>,
        watch_auto_handler: Arc<Mutex<WatchHandler<bool, WatcherAutoResponder>>>,
        watch_adjustment_handler: Arc<Mutex<WatchHandler<f32, WatcherAdjustmentResponder>>>,
    ) {
        self.handle_request(
            request,
            watch_current_handler,
            watch_auto_handler,
            watch_adjustment_handler,
        )
        .await;
    }

    async fn add_current_sender_channel(&mut self, sender: UnboundedSender<f32>) {
        self.add_current_sender_channel(sender).await;
    }

    async fn add_auto_sender_channel(&mut self, sender: UnboundedSender<bool>) {
        self.add_auto_sender_channel(sender).await;
    }

    async fn add_adjustment_sender_channel(&mut self, sender: UnboundedSender<f32>) {
        self.add_adjustment_sender_channel(sender).await;
    }

    fn get_backlight_and_auto_brightness_on(&mut self) -> (Arc<Mutex<dyn BacklightControl>>, bool) {
        self.get_backlight_and_auto_brightness_on()
    }
}

// TODO(kpt) Move all the folllowing functions into Control. This is delayed so that in the CL
// for the creation of this code the reviewer can see more easily that the code is unchanged
// after the extraction from main.rs.

fn read_brightness_table_file(path: &str) -> Result<BrightnessTable, Error> {
    let file = fs::File::open(path)?;
    let result = serde_json::from_reader(io::BufReader::new(file));
    let result = result
        .map_err(|e| anyhow::format_err!("Failed to read from file, ran into error: {:?}", e));
    result
}

fn generate_spline(table: &BrightnessTable) -> Spline<f32, f32> {
    let BrightnessTable { points } = table;
    let mut lux_to_nits_table_to_splines = Vec::new();
    for brightness_point in points {
        lux_to_nits_table_to_splines.push(Key::new(
            brightness_point.ambient_lux,
            brightness_point.display_nits,
            Interpolation::Linear,
        ));
    }
    Spline::from_iter(lux_to_nits_table_to_splines.iter().cloned())
}

// TODO(kpt) Move this and other functions into Control so that they can share the struct
// Then we won't need all these global locked variables.
/// Runs the main auto-brightness code.

async fn get_current_brightness(backlight: Arc<Mutex<dyn BacklightControl>>) -> f32 {
    let backlight = backlight.lock().await;
    let fut = backlight.get_brightness();
    // TODO(lingxueluo) Deal with this in backlight.rs later.
    match fut.await {
        Ok(brightness) => brightness as f32,
        Err(e) => {
            if *GET_BRIGHTNESS_FAILED_FIRST.lock().await {
                fx_log_warn!("Failed to get backlight: {}. assuming 1.0", e);
                *GET_BRIGHTNESS_FAILED_FIRST.lock().await = false;
            }
            *LAST_SET_BRIGHTNESS.lock().await
        }
    }
}

async fn set_current_brightness(backlight: Arc<Mutex<dyn BacklightControl>>, value: f64) {
    let mut backlight = backlight.lock().await;
    backlight
        .set_brightness(value)
        .unwrap_or_else(|e| fx_log_err!("Failed to set backlight: {}", e));
}

async fn read_sensor_and_get_brightness(
    sensor: Arc<Mutex<dyn SensorControl>>,
    spline: &Spline<f32, f32>,
    max_brightness: f64,
) -> f32 {
    let lux = {
        // Get the sensor reading in its own mutex block
        let sensor = sensor.lock().await;
        // TODO(kpt) Do we need a Mutex if sensor is only read?
        let fut = sensor.read();
        let report = fut.await.expect("Could not read from the sensor");
        report.illuminance
    };
    brightness_curve_lux_to_nits(lux, spline).await / max_brightness as f32
}

async fn brightness_curve_lux_to_nits(lux: f32, spline: &Spline<f32, f32>) -> f32 {
    let result = (*spline).clamped_sample(lux as f32);
    match result {
        Some(nits) => {
            return nits;
        }
        None => return 1.0,
    }
}

/// Sets the brightness of the backlight to a specific value.
/// An abortable task is spawned to handle this as it can take a while to do.
async fn set_brightness(
    value: f32,
    set_brightness_abort_handle: Arc<Mutex<Option<AbortHandle>>>,
    backlight: Arc<Mutex<dyn BacklightControl>>,
    current_sender_channel: Arc<Mutex<SenderChannel<f32>>>,
) {
    let value = num_traits::clamp(value, 0.0, 1.0);
    let current_value = get_current_brightness(backlight.clone()).await;
    if (current_value - value).abs() >= BRIGHTNESS_MINIMUM_CHANGE {
        let mut set_brightness_abort_handle = set_brightness_abort_handle.lock().await;
        if let Some(handle) = set_brightness_abort_handle.take() {
            handle.abort();
        }
        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        let backlight = backlight.clone();
        let current_sender_channel = current_sender_channel.clone();
        fasync::Task::spawn(
            Abortable::new(
                async move {
                    set_brightness_impl(value, backlight, current_sender_channel).await;
                },
                abort_registration,
            )
            .unwrap_or_else(|_task_aborted| ()),
        )
        .detach();
        *set_brightness_abort_handle = Some(abort_handle);
    }
}

async fn set_brightness_impl(
    value: f32,
    backlight: Arc<Mutex<dyn BacklightControl>>,
    current_sender_channel: Arc<Mutex<SenderChannel<f32>>>,
) {
    let current_value = get_current_brightness(backlight.clone()).await;
    let backlight = backlight.clone();
    let current_sender_channel = current_sender_channel.clone();
    let brightness_change_duration = BRIGHTNESS_CHANGE_DURATION.lock().await;
    let duration = *brightness_change_duration;
    drop(brightness_change_duration);
    set_brightness_slowly(current_value, value, backlight, duration, current_sender_channel).await;
}

/// Change the brightness of the screen slowly. We don't want to change the screen
/// suddenly so we smooth the transition by doing it in a series of small steps over
/// a given time. The backlight granularity is taken into account to avoid very small
/// changes to brightness that don't actually change the final value in the I2C register.
async fn set_brightness_slowly(
    current_value: f32,
    to_value: f32,
    backlight: Arc<Mutex<dyn BacklightControl>>,
    duration: Duration,
    current_sender_channel: Arc<Mutex<SenderChannel<f32>>>,
) {
    let mut current_value = current_value as f64;
    let to_value = num_traits::clamp(to_value, 0.0, 1.0) as f64;
    assert!(to_value <= 1.0);
    assert!(current_value <= 1.0);
    let current_sender_channel = current_sender_channel.clone();
    let difference = to_value - current_value;
    if difference.abs() < BRIGHTNESS_MINIMUM_CHANGE as f64 {
        return;
    }
    let mut time_per_step =
        duration.into_millis() as f64 / (BACKLIGHT_GRANULARITY * difference.abs());
    if time_per_step < BRIGHTNESS_MIN_STEP_TIME_MS {
        // Too frequent, let's slow it down. It will still look smooth.
        time_per_step = BRIGHTNESS_MIN_STEP_TIME_MS;
    }
    let sleep_time = Duration::from_millis(time_per_step as i64);
    let steps = (duration.into_millis() as f64 / time_per_step) as i64;
    if steps > 0 {
        let step_size = difference / steps as f64;
        for _i in 1..steps {
            let current_sender_channel = current_sender_channel.clone();
            current_value = current_value + step_size;
            set_current_brightness(backlight.clone(), current_value).await;
            current_sender_channel.lock().await.send_value(current_value as f32);
            // TODO(kpt): Timer::new() should take a Duration but gets an error
            fuchsia_async::Timer::new(sleep_time.after_now()).await;
        }
    }
    // Make sure we get to the correct value
    set_current_brightness(backlight.clone(), to_value).await;
    current_sender_channel.lock().await.send_value(to_value as f32);
    *LAST_SET_BRIGHTNESS.lock().await = to_value as f32;
}

#[cfg(test)]
mod tests {
    use std::path::Path;

    use anyhow::{format_err, Error};
    use async_trait::async_trait;
    use fuchsia_async::TestExecutor;
    use futures::executor::block_on;
    use futures::pin_mut;

    use crate::sender_channel::SenderChannel;
    use crate::sensor::AmbientLightInputRpt;

    use super::*;

    struct MockSensor {
        illuminence: f32,
    }

    #[async_trait]
    impl SensorControl for MockSensor {
        async fn read(&self) -> Result<AmbientLightInputRpt, Error> {
            Ok(AmbientLightInputRpt {
                illuminance: self.illuminence,
                red: 0.0,
                green: 0.0,
                blue: 0.0,
            })
        }
    }

    struct MockBacklight {
        valid_backlight: bool,
        value: f64,
        max_brightness: f64,
    }

    #[async_trait]
    impl BacklightControl for MockBacklight {
        async fn get_brightness(&self) -> Result<f64, Error> {
            if self.valid_backlight {
                Ok(self.value)
            } else {
                Err(format_err!("Get brightness failed."))
            }
        }

        fn set_brightness(&mut self, value: f64) -> Result<(), Error> {
            self.value = value;
            Ok(())
        }

        async fn get_max_absolute_brightness(&self) -> Result<f64, Error> {
            Ok(self.max_brightness)
        }
    }

    fn set_mocks(
        sensor: f32,
        backlight: f64,
    ) -> (Arc<Mutex<impl SensorControl>>, Arc<Mutex<impl BacklightControl>>) {
        let sensor = MockSensor { illuminence: sensor };
        let sensor = Arc::new(Mutex::new(sensor));
        let backlight =
            MockBacklight { valid_backlight: true, value: backlight, max_brightness: 250.0 };
        let backlight = Arc::new(Mutex::new(backlight));
        (sensor, backlight)
    }

    fn set_mocks_not_valid(
        sensor: f32,
        backlight: f64,
    ) -> (Arc<Mutex<impl SensorControl>>, Arc<Mutex<impl BacklightControl>>) {
        let sensor = MockSensor { illuminence: sensor };
        let sensor = Arc::new(Mutex::new(sensor));
        let backlight =
            MockBacklight { valid_backlight: false, value: backlight, max_brightness: 250.0 };
        let backlight = Arc::new(Mutex::new(backlight));
        (sensor, backlight)
    }

    async fn generate_control_struct(sensor: f32, backlight: f64) -> Control {
        let (sensor, backlight) = set_mocks(sensor, backlight);
        let set_brightness_abort_handle = Arc::new(Mutex::new(None::<AbortHandle>));
        let auto_brightness_abort_handle = None::<AbortHandle>;
        let BrightnessTable { points } = &*BRIGHTNESS_TABLE.lock().await;
        let brightness_table_old = BrightnessTable { points: points.to_vec() };
        let spline_arg = generate_spline(brightness_table_old.clone());

        let current_sender_channel: SenderChannel<f32> = SenderChannel::new();
        let current_sender_channel = Arc::new(Mutex::new(current_sender_channel));

        let auto_sender_channel: SenderChannel<bool> = SenderChannel::new();
        let auto_sender_channel = Arc::new(Mutex::new(auto_sender_channel));

        let adjustment_sender_channel: SenderChannel<f32> = SenderChannel::new();
        let adjustment_sender_channel = Arc::new(Mutex::new(adjustment_sender_channel));

        Control {
            sensor,
            backlight,
            set_brightness_abort_handle,
            auto_brightness_abort_handle,
            spline: spline_arg,
            current_sender_channel,
            auto_sender_channel,
            adjustment_sender_channel,
        }
    }

    fn generate_spline(table: BrightnessTable) -> Spline<f32, f32> {
        let BrightnessTable { points } = &table;
        let mut lux_to_nits_table_to_splines = Vec::new();
        for brightness_point in points {
            lux_to_nits_table_to_splines.push(Key::new(
                brightness_point.ambient_lux,
                brightness_point.display_nits,
                Interpolation::Linear,
            ));
        }
        Spline::from_iter(lux_to_nits_table_to_splines.iter().cloned())
    }

    fn cmp_float(value1: f32, value2: f32) -> bool {
        (value1 - value2).abs() < 0.01
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_brightness_curve() {
        let BrightnessTable { points } = &*BRIGHTNESS_TABLE.lock().await;
        let brightness_table = BrightnessTable { points: points.to_vec() };
        let spline = generate_spline(brightness_table);

        assert_eq!(cmp_float(0., brightness_curve_lux_to_nits(0.0, &spline).await), true);
        assert_eq!(cmp_float(0.333, brightness_curve_lux_to_nits(1.0, &spline).await), true);
        assert_eq!(cmp_float(0.666, brightness_curve_lux_to_nits(2.0, &spline).await), true);
        assert_eq!(cmp_float(4.67, brightness_curve_lux_to_nits(15.0, &spline).await), true);
        assert_eq!(cmp_float(4.94, brightness_curve_lux_to_nits(16.0, &spline).await), true);
        assert_eq!(cmp_float(32.78, brightness_curve_lux_to_nits(100.0, &spline).await), true);
        assert_eq!(cmp_float(36.82, brightness_curve_lux_to_nits(150.0, &spline).await), true);
        assert_eq!(cmp_float(68.63, brightness_curve_lux_to_nits(200.0, &spline).await), true);
        assert_eq!(cmp_float(111.87, brightness_curve_lux_to_nits(240.0, &spline).await), true);
        assert_eq!(cmp_float(162.96, brightness_curve_lux_to_nits(300.0, &spline).await), true);
        assert_eq!(cmp_float(300., brightness_curve_lux_to_nits(340.0, &spline).await), true);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_brightness_table_valid() {
        let mut control = generate_control_struct(400.0, 0.5).await;
        let brightness_table = {
            let mut lux_to_nits = Vec::new();
            lux_to_nits.push(BrightnessPoint { ambient_lux: 10., display_nits: 50. });
            lux_to_nits.push(BrightnessPoint { ambient_lux: 30., display_nits: 50. });
            lux_to_nits.push(BrightnessPoint { ambient_lux: 60., display_nits: 50. });
            lux_to_nits.push(BrightnessPoint { ambient_lux: 100., display_nits: 50. });
            lux_to_nits.push(BrightnessPoint { ambient_lux: 150., display_nits: 50. });
            lux_to_nits.push(BrightnessPoint { ambient_lux: 210., display_nits: 50. });
            lux_to_nits.push(BrightnessPoint { ambient_lux: 250., display_nits: 50. });
            lux_to_nits.push(BrightnessPoint { ambient_lux: 300., display_nits: 50. });

            BrightnessTable { points: lux_to_nits }
        };
        control.check_brightness_table_and_set_new_curve(&brightness_table).await.unwrap();
        assert_eq!(cmp_float(50.0, brightness_curve_lux_to_nits(0.0, &control.spline).await), true);
        assert_eq!(cmp_float(50.0, brightness_curve_lux_to_nits(1.0, &control.spline).await), true);
        assert_eq!(cmp_float(50.0, brightness_curve_lux_to_nits(2.0, &control.spline).await), true);
        assert_eq!(
            cmp_float(50.0, brightness_curve_lux_to_nits(15.0, &control.spline).await),
            true
        );
        assert_eq!(
            cmp_float(50.0, brightness_curve_lux_to_nits(16.0, &control.spline).await),
            true
        );
        assert_eq!(
            cmp_float(50.0, brightness_curve_lux_to_nits(100.0, &control.spline).await),
            true
        );
        assert_eq!(
            cmp_float(50.0, brightness_curve_lux_to_nits(150.0, &control.spline).await),
            true
        );
        assert_eq!(
            cmp_float(50.0, brightness_curve_lux_to_nits(200.0, &control.spline).await),
            true
        );
        assert_eq!(
            cmp_float(50.0, brightness_curve_lux_to_nits(240.0, &control.spline).await),
            true
        );
        assert_eq!(
            cmp_float(50.0, brightness_curve_lux_to_nits(300.0, &control.spline).await),
            true
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_brightness_table_not_valid_negative_value() {
        let mut control = generate_control_struct(400.0, 0.5).await;
        let brightness_table = {
            let mut lux_to_nits = Vec::new();
            lux_to_nits.push(BrightnessPoint { ambient_lux: -10., display_nits: 50. });
            lux_to_nits.push(BrightnessPoint { ambient_lux: 30., display_nits: 50. });
            BrightnessTable { points: lux_to_nits }
        };
        control.check_brightness_table_and_set_new_curve(&brightness_table).await.unwrap_err();
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_brightness_table_not_valid_lux_not_increasing() {
        let mut control = generate_control_struct(400.0, 0.5).await;
        let brightness_table = {
            let mut lux_to_nits = Vec::new();
            lux_to_nits.push(BrightnessPoint { ambient_lux: 10., display_nits: 50. });
            lux_to_nits.push(BrightnessPoint { ambient_lux: 30., display_nits: 50. });
            lux_to_nits.push(BrightnessPoint { ambient_lux: 3., display_nits: 50. });
            lux_to_nits.push(BrightnessPoint { ambient_lux: 100., display_nits: 50. });
            BrightnessTable { points: lux_to_nits }
        };
        control.check_brightness_table_and_set_new_curve(&brightness_table).await.unwrap_err();
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_brightness_table_valid_but_empty() {
        let mut control = generate_control_struct(400.0, 0.5).await;
        let brightness_table = {
            let lux_to_nits = Vec::new();
            BrightnessTable { points: lux_to_nits }
        };
        let old_curve = &control.spline.clone();
        control.check_brightness_table_and_set_new_curve(&brightness_table).await.unwrap();
        assert_eq!(
            cmp_float(
                brightness_curve_lux_to_nits(0.0, old_curve).await,
                brightness_curve_lux_to_nits(0.0, &control.spline).await,
            ),
            true
        );
        assert_eq!(
            cmp_float(
                brightness_curve_lux_to_nits(1.0, old_curve).await,
                brightness_curve_lux_to_nits(1.0, &control.spline).await,
            ),
            true
        );
        assert_eq!(
            cmp_float(
                brightness_curve_lux_to_nits(2.0, old_curve).await,
                brightness_curve_lux_to_nits(2.0, &control.spline).await,
            ),
            true
        );
        assert_eq!(
            cmp_float(
                brightness_curve_lux_to_nits(15.0, old_curve).await,
                brightness_curve_lux_to_nits(15.0, &control.spline).await,
            ),
            true
        );
        assert_eq!(
            cmp_float(
                brightness_curve_lux_to_nits(16.0, old_curve).await,
                brightness_curve_lux_to_nits(16.0, &control.spline).await,
            ),
            true
        );
        assert_eq!(
            cmp_float(
                brightness_curve_lux_to_nits(100.0, old_curve).await,
                brightness_curve_lux_to_nits(100.0, &control.spline).await,
            ),
            true
        );
        assert_eq!(
            cmp_float(
                brightness_curve_lux_to_nits(150.0, old_curve).await,
                brightness_curve_lux_to_nits(150.0, &control.spline).await,
            ),
            true
        );
        assert_eq!(
            cmp_float(
                brightness_curve_lux_to_nits(200.0, old_curve).await,
                brightness_curve_lux_to_nits(200.0, &control.spline).await,
            ),
            true
        );
        assert_eq!(
            cmp_float(
                brightness_curve_lux_to_nits(240.0, old_curve).await,
                brightness_curve_lux_to_nits(240.0, &control.spline).await,
            ),
            true
        );
        assert_eq!(
            cmp_float(
                brightness_curve_lux_to_nits(300.0, old_curve).await,
                brightness_curve_lux_to_nits(300.0, &control.spline).await,
            ),
            true
        );
        assert_eq!(
            cmp_float(
                brightness_curve_lux_to_nits(340.0, old_curve).await,
                brightness_curve_lux_to_nits(340.0, &control.spline).await,
            ),
            true
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_scale_new_adjustment() {
        let mut control = generate_control_struct(400.0, 0.5).await;
        let mut new_adjust = control.scale_new_adjustment(-1.0).await;
        assert_eq!(0.25, new_adjust);
        new_adjust = control.scale_new_adjustment(1.0).await;
        assert_eq!(8.0, new_adjust);
        new_adjust = control.scale_new_adjustment(0.0).await;
        assert_eq!(1.0, new_adjust);
    }

    fn assert_float_f32(expected: f32, value: f32) {
        assert!(
            (expected - value as f32).abs() < 0.00001,
            "expected {}, found {}",
            expected,
            value
        );
    }

    #[test]
    fn test_set_brightness_slowly_send_value() {
        // Need to use a TestExecutor with fake time to run past the timeouts without delay
        let mut exec = TestExecutor::new_with_fake_time().expect("executor needed");
        exec.set_fake_time(fasync::Time::from_nanos(0));

        let control = block_on(generate_control_struct(400.0, 0.5));
        let (channel_sender, mut channel_receiver) = futures::channel::mpsc::unbounded::<f32>();
        {
            let mut channel = block_on(control.current_sender_channel.lock());
            block_on(channel.add_sender_channel(channel_sender));
        }

        // This will produce 10 50ms steps of 0.002 brightness
        let future = set_brightness_slowly(
            0.4,
            0.42,
            control.backlight,
            500.millis(),
            control.current_sender_channel,
        );
        pin_mut!(future);

        for _i in 1..10 {
            assert!(exec.run_until_stalled(&mut future).is_pending());
            if let Some(deadline) = exec.wake_next_timer() {
                let deadline = deadline.into_nanos().nanos();
                assert_eq!(50, deadline.into_millis());
            } else {
                panic!("Timer has no value");
            }
        }
        // Make sure we have finished
        assert!(exec.run_until_stalled(&mut future).is_ready());

        // Check the receiver's results
        assert_float_f32(0.402, block_on(channel_receiver.next()).unwrap());
        assert_float_f32(0.404, block_on(channel_receiver.next()).unwrap());
        assert_float_f32(0.406, block_on(channel_receiver.next()).unwrap());
        assert_float_f32(0.408, block_on(channel_receiver.next()).unwrap());
        assert_float_f32(0.410, block_on(channel_receiver.next()).unwrap());
        assert_float_f32(0.412, block_on(channel_receiver.next()).unwrap());
        assert_float_f32(0.414, block_on(channel_receiver.next()).unwrap());
        assert_float_f32(0.416, block_on(channel_receiver.next()).unwrap());
        assert_float_f32(0.418, block_on(channel_receiver.next()).unwrap());
        assert_float_f32(0.420, block_on(channel_receiver.next()).unwrap());
    }

    #[test]
    fn test_set_brightness_slowly_long_timeout() {
        // Need to use a TestExecutor with fake time to run past the timeouts without delay
        let mut exec = TestExecutor::new_with_fake_time().expect("executor needed");
        exec.set_fake_time(fasync::Time::from_nanos(0));

        let control = block_on(generate_control_struct(400.0, 0.5));
        let (channel_sender, mut channel_receiver) = futures::channel::mpsc::unbounded::<f32>();
        {
            let mut channel = block_on(control.current_sender_channel.lock());
            block_on(channel.add_sender_channel(channel_sender));
        }

        // This will produce 2 approx 5sec steps of -0.00025 brightness
        let future = set_brightness_slowly(
            0.0006,
            0.0001,
            control.backlight,
            10.seconds(),
            control.current_sender_channel,
        );
        pin_mut!(future);

        for _i in 1..2 {
            assert!(exec.run_until_stalled(&mut future).is_pending());
            if let Some(deadline) = exec.wake_next_timer() {
                let deadline = deadline.into_nanos().nanos();
                assert!(deadline > 4500.millis());
                assert!(deadline < 5000.millis());
            } else {
                panic!("Timer has no value");
            }
        }
        // Make sure we have finished
        assert!(exec.run_until_stalled(&mut future).is_ready());

        // Check the receiver's results
        assert_float_f32(0.00035, block_on(channel_receiver.next()).unwrap());
        assert_float_f32(0.0001, block_on(channel_receiver.next()).unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_sensor_and_get_brightness_bright() {
        let BrightnessTable { points } = &*BRIGHTNESS_TABLE.lock().await;
        let brightness_table = BrightnessTable { points: points.to_vec() };
        let spline = generate_spline(brightness_table);
        let (sensor, _backlight) = set_mocks(400.0, 1.5);
        let value = read_sensor_and_get_brightness(sensor, &spline, 250.0).await;
        assert_eq!(cmp_float(1.2, value), true);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_sensor_and_get_brightness_low_light() {
        let BrightnessTable { points } = &*BRIGHTNESS_TABLE.lock().await;
        let brightness_table = BrightnessTable { points: points.to_vec() };
        let spline = generate_spline(brightness_table);
        let (sensor, _backlight) = set_mocks(0.0, 0.0);
        let value = read_sensor_and_get_brightness(sensor, &spline, 250.0).await;
        assert_eq!(cmp_float(0.0, value), true);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_brightness_is_abortable_with_auto_brightness_on() {
        let control = generate_control_struct(400.0, 0.5).await;
        let set_brightness_abort_handle = Arc::new(Mutex::new(None::<AbortHandle>));
        let (_sensor, backlight) = set_mocks(0.0, 0.0);
        let backlight = backlight.clone();
        set_brightness(
            0.04,
            set_brightness_abort_handle.clone(),
            backlight.clone(),
            control.current_sender_channel,
        )
        .await;
        // Abort the task before it really gets going
        let mut set_brightness_abort_handle = set_brightness_abort_handle.lock().await;
        if let Some(handle) = set_brightness_abort_handle.take() {
            handle.abort();
        }
        // It should not have reached the final value yet.
        // We know that set_brightness_slowly, at the bottom of the task, finishes at the correct
        // value from other tests if it has sufficient time.
        let backlight = backlight.lock().await;

        assert_ne!(cmp_float(0.04, backlight.get_brightness().await.unwrap() as f32), true);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_brightness_impl() {
        let control = generate_control_struct(400.0, 0.5).await;
        let (_sensor, backlight) = set_mocks(0.0, 0.0);
        let backlight_clone = backlight.clone();
        set_brightness_impl(0.3, backlight_clone, control.current_sender_channel).await;
        let backlight = backlight.lock().await;
        assert_eq!(cmp_float(0.3, backlight.get_brightness().await.unwrap() as f32), true);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_brightness_manager_fail_gracefully() {
        let control = generate_control_struct(400.0, 0.5).await;
        let (_sensor, backlight) = set_mocks_not_valid(0.0, 0.0);
        {
            let last_set_brightness = &*LAST_SET_BRIGHTNESS.lock().await;
            assert_eq!(cmp_float(*last_set_brightness, 1.0), true);
        }
        let backlight_clone = backlight.clone();
        set_brightness_impl(0.04, backlight_clone, control.current_sender_channel).await;
        let last_set_brightness = &*LAST_SET_BRIGHTNESS.lock().await;
        assert_eq!(cmp_float(*last_set_brightness, 0.04), true);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_store_brightness_table() -> Result<(), Error> {
        let mut control = generate_control_struct(400.0, 0.5).await;
        let brightness_table = BrightnessTable {
            points: vec![
                BrightnessPoint { ambient_lux: 10., display_nits: 50. },
                BrightnessPoint { ambient_lux: 30., display_nits: 50. },
                BrightnessPoint { ambient_lux: 60., display_nits: 50. },
            ],
        };
        control.store_brightness_table(&brightness_table, "/data/test_brightness_file")?;
        let file = fs::File::open("/data/test_brightness_file")?;
        let data = serde_json::from_reader(io::BufReader::new(file))?;
        let BrightnessTable { points: read_points } = data;
        let BrightnessTable { points: write_points } = brightness_table;
        let iter = read_points.iter().zip(write_points.iter());
        for point_tuple in iter {
            let (read_point, write_point) = point_tuple;
            assert_eq!(cmp_float(read_point.ambient_lux, write_point.ambient_lux), true);
            assert_eq!(cmp_float(read_point.display_nits, write_point.display_nits), true);
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_brightness_table_file_empty_file() {
        fs::File::create("/data/empty_file").unwrap();
        let result = read_brightness_table_file("/data/empty_file");
        assert_eq!(true, result.is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_brightness_table_file_missing_file() {
        assert_eq!(false, Path::new("/data/nonexistent_file").exists());
        let result = read_brightness_table_file("/data/nonexistent_file");
        assert_eq!(true, result.is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_brightness_table_file_invalid_data() {
        let file = fs::File::create("/data/invalid_brightness_file").unwrap();
        serde_json::to_writer(io::BufWriter::new(file), &1.0).unwrap();
        let result = read_brightness_table_file("/data/invalid_brightness_file");
        assert_eq!(true, result.is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_manual_brightness_smooth_with_duration_got_set() {
        let mut control = generate_control_struct(400.0, 0.5).await;
        control.set_manual_brightness_smooth(0.6, Duration::from_nanos(4000000000)).await;
        let duration = *BRIGHTNESS_CHANGE_DURATION.lock().await;
        assert_eq!(4000, duration.into_millis());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_manual_brightness_set_duration() {
        let mut control = generate_control_struct(400.0, 0.5).await;
        control.set_manual_brightness(0.6).await;
        let duration = *BRIGHTNESS_CHANGE_DURATION.lock().await;
        assert_eq!(250, duration.into_millis());
    }

    #[test]
    fn test_set_manual_brightness_updates_brightness() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let func_fut1 = generate_control_struct(400.0, 0.5);
        futures::pin_mut!(func_fut1);
        let mut control = exec.run_singlethreaded(&mut func_fut1);
        {
            let func_fut2 = control.set_manual_brightness_smooth(0.6, Duration::from_nanos(4000));
            futures::pin_mut!(func_fut2);
            exec.run_singlethreaded(&mut func_fut2);
        }
        let _ = exec.run_until_stalled(&mut future::pending::<()>());
        let func_fut3 = control.backlight.lock();
        futures::pin_mut!(func_fut3);
        let backlight = exec.run_singlethreaded(&mut func_fut3);
        let func_fut4 = backlight.get_brightness();
        futures::pin_mut!(func_fut4);
        let value = exec.run_singlethreaded(&mut func_fut4);
        assert_eq!(cmp_float(0.6, value.unwrap() as f32), true);
    }

    #[test]
    fn test_set_manual_brightness_updates_brightness_small_change() {
        const TARGET_BRIGHTNESS: f32 = 0.0006;
        const ORIGINAL_BRIGHTNESS: f32 = 0.001;
        assert!((TARGET_BRIGHTNESS - ORIGINAL_BRIGHTNESS).abs() > BRIGHTNESS_MINIMUM_CHANGE);

        let mut exec = fasync::TestExecutor::new().unwrap();
        let func_fut1 = generate_control_struct(400.0, ORIGINAL_BRIGHTNESS as f64);
        futures::pin_mut!(func_fut1);
        let mut control = exec.run_singlethreaded(&mut func_fut1);
        {
            let func_fut2 =
                control.set_manual_brightness_smooth(0.0006, Duration::from_nanos(4000));
            futures::pin_mut!(func_fut2);
            exec.run_singlethreaded(&mut func_fut2);
        }
        let _ = exec.run_until_stalled(&mut future::pending::<()>());
        let func_fut3 = control.backlight.lock();
        futures::pin_mut!(func_fut3);
        let backlight = exec.run_singlethreaded(&mut func_fut3);
        let func_fut4 = backlight.get_brightness();
        futures::pin_mut!(func_fut4);
        let value = exec.run_singlethreaded(&mut func_fut4);
        let brightness_value = value.unwrap() as f32;
        assert_eq!(TARGET_BRIGHTNESS, brightness_value);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_max_absolute_brightness() {
        let mut control = generate_control_struct(400.0, 0.5).await;
        let max_brightness = control.get_max_absolute_brightness().await;
        assert_eq!(250.0, max_brightness.unwrap());
    }
}
