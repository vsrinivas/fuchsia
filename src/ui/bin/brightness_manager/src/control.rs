use crate::backlight::BacklightControl;
use crate::sender_channel::SenderChannel;
use crate::sensor::SensorControl;
use async_trait::async_trait;
use std::sync::Arc;

use failure::Error;
use fidl_fuchsia_ui_brightness::{
    BrightnessPoint, BrightnessTable, ControlRequest as BrightnessControlRequest,
    ControlWatchAutoBrightnessOffsetResponder, ControlWatchAutoBrightnessResponder,
    ControlWatchCurrentBrightnessResponder,
};
use fuchsia_async::{self as fasync, DurationExt};
use fuchsia_syslog::{self, fx_log_err, fx_log_info};
use fuchsia_zircon::{Duration, DurationNum};
use futures::channel::mpsc::UnboundedSender;
use futures::future::{AbortHandle, Abortable};
use futures::lock::Mutex;
use futures::prelude::*;
use lazy_static::lazy_static;
use splines::{Interpolation, Key, Spline};
use watch_handler::{Sender, WatchHandler};

// Delay between sensor reads
const SLOW_SCAN_TIMEOUT_MS: i64 = 2000;
// Delay if we have made a large change in auto brightness
const QUICK_SCAN_TIMEOUT_MS: i64 = 100;
// What constitutes a large change in brightness?
// This seems small but it is significant and works nicely.
const LARGE_CHANGE_THRESHOLD_NITS: i32 = 0.016 as i32;
const AUTO_MINIMUM_BRIGHTNESS: f32 = 0.004;

//This is the default table, and a default curve will be generated base on this table.
//This will be replaced once SetBrightnessTable is called.
lazy_static! {
    static ref BRIGHTNESS_TABLE: Arc<Mutex<BrightnessTable>> = {
        let mut lux_to_nits = Vec::new();
        lux_to_nits.push(BrightnessPoint { ambient_lux: 0., display_nits: 0. });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 10., display_nits: 3.33 });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 30., display_nits: 13.02 });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 60., display_nits: 26.19 });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 100., display_nits: 36.87 });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 150., display_nits: 54.12 });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 210., display_nits: 112.34 });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 250., display_nits: 124.16 });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 300., display_nits: 162.96 });
        lux_to_nits.push(BrightnessPoint { ambient_lux: 340., display_nits: 300. });
        Arc::new(Mutex::new(BrightnessTable { points: lux_to_nits }))
    };
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

pub struct WatcherOffsetResponder {
    watcher_offset_responder: ControlWatchAutoBrightnessOffsetResponder,
}

impl Sender<f32> for WatcherOffsetResponder {
    fn send_response(self, data: f32) {
        if let Err(e) = self.watcher_offset_responder.send(data) {
            fx_log_err!("Failed to reply to WatchAutoBrightnessOffset: {}", e);
        }
    }
}

pub struct Control {
    sensor: Arc<Mutex<dyn SensorControl>>,
    backlight: Arc<Mutex<dyn BacklightControl>>,
    set_brightness_abort_handle: Option<AbortHandle>,
    auto_brightness_abort_handle: Option<AbortHandle>,
    spline: Spline<f32, f32>,
    current_sender_channel: Arc<Mutex<SenderChannel<f32>>>,
    auto_sender_channel: Arc<Mutex<SenderChannel<bool>>>,
    offset_sender_channel: Arc<Mutex<SenderChannel<f32>>>,
}

impl Control {
    pub async fn new(
        sensor: Arc<Mutex<dyn SensorControl>>,
        backlight: Arc<Mutex<dyn BacklightControl>>,
        current_sender_channel: Arc<Mutex<SenderChannel<f32>>>,
        auto_sender_channel: Arc<Mutex<SenderChannel<bool>>>,
        offset_sender_channel: Arc<Mutex<SenderChannel<f32>>>,
    ) -> Control {
        fx_log_info!("New Control class");

        let set_brightness_abort_handle = None::<AbortHandle>;

        let BrightnessTable { points } = &*BRIGHTNESS_TABLE.lock().await;
        let brightness_table = BrightnessTable { points: points.to_vec() };
        let spline_arg = generate_spline(&brightness_table);

        // Startup auto-brightness loop
        let initial_auto_brightness_abort_handle = None::<AbortHandle>;
        let auto_brightness_abort_handle = start_auto_brightness_task(
            sensor.clone(),
            backlight.clone(),
            spline_arg.clone(),
            initial_auto_brightness_abort_handle.as_ref(),
            current_sender_channel.clone(),
        );

        Control {
            backlight,
            sensor,
            set_brightness_abort_handle,
            auto_brightness_abort_handle,
            spline: spline_arg,
            current_sender_channel,
            auto_sender_channel,
            offset_sender_channel,
        }
    }

    pub async fn handle_request(
        &mut self,
        request: BrightnessControlRequest,
        watch_current_handler: Arc<Mutex<WatchHandler<f32, WatcherCurrentResponder>>>,
        watch_auto_handler: Arc<Mutex<WatchHandler<bool, WatcherAutoResponder>>>,
        watch_offset_handler: Arc<Mutex<WatchHandler<f32, WatcherOffsetResponder>>>,
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
                    Ok(_v) => fx_log_info!("Sent the auto value"),
                    Err(e) => fx_log_info!("Didn't watch auto value successfully, got err {}", e),
                }
            }
            BrightnessControlRequest::SetManualBrightness { value, control_handle: _ } => {
                self.set_manual_brightness(value).await;
            }
            BrightnessControlRequest::WatchCurrentBrightness { responder } => {
                let watch_current_result =
                    self.watch_current_brightness(watch_current_handler, responder).await;
                match watch_current_result {
                    Ok(_v) => fx_log_info!("Sent the current value"),
                    Err(e) => {
                        fx_log_info!("Didn't watch current value successfully, got err {}", e)
                    }
                }
            }
            BrightnessControlRequest::SetBrightnessTable { table, control_handle: _ } => {
                self.set_brightness_table(&table).await;
                *BRIGHTNESS_TABLE.lock().await = table;
            }

            BrightnessControlRequest::SetAutoBrightnessOffset { offset, control_handle: _ } => {
                self.set_auto_brightness_offset(offset).await;
            }
            BrightnessControlRequest::WatchAutoBrightnessOffset { responder } => {
                let watch_offset_result =
                    self.watch_auto_brightness_offset(watch_offset_handler, responder).await;
                match watch_offset_result {
                    Ok(_v) => fx_log_info!("Sent the current value"),
                    Err(e) => fx_log_info!(
                        "Didn't watch auto brightness offset successfully, got err {}",
                        e
                    ),
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

    pub async fn add_offset_sender_channel(&mut self, sender: UnboundedSender<f32>) {
        self.offset_sender_channel.lock().await.add_sender_channel(sender).await;
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
            self.auto_brightness_abort_handle = start_auto_brightness_task(
                self.sensor.clone(),
                self.backlight.clone(),
                self.spline.clone(),
                self.auto_brightness_abort_handle.as_ref(),
                self.current_sender_channel.clone(),
            );
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
        fx_log_info!("Received get auto brightness enabled");
        let mut hanging_get_lock = watch_auto_handler.lock().await;
        hanging_get_lock.watch(WatcherAutoResponder { watcher_auto_responder: responder })?;
        Ok(())
    }

    async fn set_manual_brightness(&mut self, value: f32) {
        // Stop the background brightness tasks, if any
        if let Some(handle) = self.set_brightness_abort_handle.take() {
            handle.abort();
        }

        if let Some(handle) = self.auto_brightness_abort_handle.take() {
            fx_log_info!("Auto-brightness off, brightness set to {}", value);
            handle.abort();
            self.auto_sender_channel
                .lock()
                .await
                .send_value(self.auto_brightness_abort_handle.is_some());
        }

        // TODO(b/138455663): remove this when the driver changes.
        let value = num_traits::clamp(value, 0.0, 1.0);
        let backlight_clone = self.backlight.clone();
        self.set_brightness_abort_handle = Some(set_brightness(value, backlight_clone).await);
        self.current_sender_channel.lock().await.send_value(value);
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

    async fn set_brightness_table(&mut self, table: &BrightnessTable) {
        fx_log_info!("Setting brightness table.");
        self.spline = generate_spline(table);

        if self.auto_brightness_abort_handle.is_some() {
            self.auto_brightness_abort_handle = start_auto_brightness_task(
                self.sensor.clone(),
                self.backlight.clone(),
                self.spline.clone(),
                self.auto_brightness_abort_handle.as_ref(),
                self.current_sender_channel.clone(),
            );
        }
    }

    async fn set_auto_brightness_offset(&mut self, offset: f32) {
        let old_table = {
            let BrightnessTable { points } = &*BRIGHTNESS_TABLE.lock().await;
            BrightnessTable { points: points.to_vec() }
        };
        let BrightnessTable { points } = &old_table;
        let mut new_table = Vec::new();
        for brightness_point in points {
            new_table.push(BrightnessPoint {
                ambient_lux: brightness_point.ambient_lux,
                display_nits: brightness_point.display_nits * offset,
            });
        }
        let new_table = BrightnessTable { points: new_table };
        self.set_brightness_table(&new_table).await;
        self.offset_sender_channel.lock().await.send_value(offset);
    }

    async fn watch_auto_brightness_offset(
        &mut self,
        watch_offset_handler: Arc<Mutex<WatchHandler<f32, WatcherOffsetResponder>>>,
        responder: ControlWatchAutoBrightnessOffsetResponder,
    ) -> Result<(), Error> {
        let mut hanging_get_lock = watch_offset_handler.lock().await;
        hanging_get_lock.watch(WatcherOffsetResponder { watcher_offset_responder: responder })?;
        Ok(())
    }
}

#[async_trait(?Send)]
pub trait ControlTrait {
    async fn handle_request(
        &mut self,
        request: BrightnessControlRequest,
        watch_current_handler: Arc<Mutex<WatchHandler<f32, WatcherCurrentResponder>>>,
        watch_auto_handler: Arc<Mutex<WatchHandler<bool, WatcherAutoResponder>>>,
        watch_offset_handler: Arc<Mutex<WatchHandler<f32, WatcherOffsetResponder>>>,
    );
    async fn add_current_sender_channel(&mut self, sender: UnboundedSender<f32>);
    async fn add_auto_sender_channel(&mut self, sender: UnboundedSender<bool>);
    async fn add_offset_sender_channel(&mut self, sender: UnboundedSender<f32>);
    fn get_backlight_and_auto_brightness_on(&mut self) -> (Arc<Mutex<dyn BacklightControl>>, bool);
}

#[async_trait(?Send)]
impl ControlTrait for Control {
    async fn handle_request(
        &mut self,
        request: BrightnessControlRequest,
        watch_current_handler: Arc<Mutex<WatchHandler<f32, WatcherCurrentResponder>>>,
        watch_auto_handler: Arc<Mutex<WatchHandler<bool, WatcherAutoResponder>>>,
        watch_offset_handler: Arc<Mutex<WatchHandler<f32, WatcherOffsetResponder>>>,
    ) {
        self.handle_request(
            request,
            watch_current_handler,
            watch_auto_handler,
            watch_offset_handler,
        )
        .await;
    }

    async fn add_current_sender_channel(&mut self, sender: UnboundedSender<f32>) {
        self.add_current_sender_channel(sender).await;
    }

    async fn add_auto_sender_channel(&mut self, sender: UnboundedSender<bool>) {
        self.add_auto_sender_channel(sender).await;
    }

    async fn add_offset_sender_channel(&mut self, sender: UnboundedSender<f32>) {
        self.add_offset_sender_channel(sender).await;
    }

    fn get_backlight_and_auto_brightness_on(&mut self) -> (Arc<Mutex<dyn BacklightControl>>, bool) {
        self.get_backlight_and_auto_brightness_on()
    }
}

// TODO(kpt) Move all the folllowing functions into Control. This is delayed so that in the CL
// for the creation of this code the reviewer can see more easily that the code is unchanged
// after the extraction from main.rs.

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
/// Runs the main auto-brightness code.
/// This task monitors its running boolean and terminates if it goes false.
fn start_auto_brightness_task(
    sensor: Arc<Mutex<dyn SensorControl>>,
    backlight: Arc<Mutex<dyn BacklightControl>>,
    spline: Spline<f32, f32>,
    auto_brightness_abort_handle: Option<&AbortHandle>,
    sender_channel: Arc<Mutex<SenderChannel<f32>>>,
) -> Option<AbortHandle> {
    if let Some(handle) = auto_brightness_abort_handle {
        handle.abort();
    }
    let (abort_handle, abort_registration) = AbortHandle::new_pair();
    fasync::spawn(
        Abortable::new(
            async move {
                let max_brightness = {
                    let backlight = backlight.lock().await;
                    backlight.get_max_absolute_brightness()
                };
                let mut set_brightness_abort_handle = None::<AbortHandle>;
                // initialize to an impossible number
                let mut last_value: i32 = -1;
                loop {
                    let sensor = sensor.clone();
                    let mut value =
                        read_sensor_and_get_brightness(sensor, &spline, max_brightness).await;
                    let backlight_clone = backlight.clone();
                    if let Some(handle) = set_brightness_abort_handle {
                        handle.abort();
                    }
                    value = num_traits::clamp(value, AUTO_MINIMUM_BRIGHTNESS, 1.0);
                    set_brightness_abort_handle =
                        Some(set_brightness(value, backlight_clone).await);
                    sender_channel.lock().await.send_value(value);
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
    );
    Some(abort_handle)
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

async fn brightness_curve_lux_to_nits(lux: u16, spline: &Spline<f32, f32>) -> f32 {
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
async fn set_brightness(value: f32, backlight: Arc<Mutex<dyn BacklightControl>>) -> AbortHandle {
    let (abort_handle, abort_registration) = AbortHandle::new_pair();
    fasync::spawn(
        Abortable::new(
            async move {
                let mut backlight = backlight.lock().await;
                let current_value = {
                    let fut = backlight.get_brightness();
                    fut.await.unwrap_or_else(|e| {
                        fx_log_err!("Failed to get backlight: {}. assuming 1.0", e);
                        1.0
                    }) as f32
                };
                let set_brightness = |value| {
                    backlight
                        .set_brightness(value)
                        .unwrap_or_else(|e| fx_log_err!("Failed to set backlight: {}", e))
                };
                set_brightness_slowly(current_value, value, set_brightness, 10.millis()).await;
            },
            abort_registration,
        )
        .unwrap_or_else(|_task_aborted| ()),
    );
    abort_handle
}

/// Change the brightness of the screen slowly to `nits` nits. We don't want to change the screen
/// suddenly so we smooth the transition by doing it in a series of small steps.
/// The time per step can be changed if needed e.g. to fade up slowly and down quickly.
/// When testing we set time_per_step to zero.
async fn set_brightness_slowly(
    current_value: f32,
    to_value: f32,
    mut set_brightness: impl FnMut(f64),
    time_per_step: Duration,
) {
    let mut current_value = current_value;
    let to_value = num_traits::clamp(to_value, 0.0, 1.0);
    assert!(to_value <= 1.0);
    assert!(current_value <= 1.0);
    let difference = to_value - current_value;
    // TODO(kpt): Steps is determined basing on the change size, change when driver accepts more values (b/138455166)
    let steps = (difference.abs() / 0.005) as u16;

    if steps > 0 {
        let step_size = difference / steps as f32;
        for _i in 0..steps {
            current_value = current_value + step_size;
            set_brightness(current_value as f64);
            if time_per_step.into_millis() > 0 {
                fuchsia_async::Timer::new(time_per_step.after_now()).await;
            }
        }
    }
    // Make sure we get to the correct value, there may be rounding errors

    set_brightness(to_value as f64);
}

#[cfg(test)]

mod tests {
    use super::*;

    use crate::sender_channel::SenderChannel;
    use crate::sensor::AmbientLightInputRpt;
    use async_trait::async_trait;
    use failure::Error;

    struct MockSensor {
        illuminence: u16,
    }

    #[async_trait]
    impl SensorControl for MockSensor {
        async fn read(&self) -> Result<AmbientLightInputRpt, Error> {
            Ok(AmbientLightInputRpt {
                rpt_id: 0,
                state: 0,
                event: 0,
                illuminance: self.illuminence,
                red: 0,
                green: 0,
                blue: 0,
            })
        }
    }

    struct MockBacklight {
        value: f64,
        max_brightness: f64,
    }

    #[async_trait]
    impl BacklightControl for MockBacklight {
        async fn get_brightness(&self) -> Result<f64, Error> {
            Ok(self.value)
        }

        fn set_brightness(&mut self, value: f64) -> Result<(), Error> {
            self.value = value;
            Ok(())
        }

        fn get_max_absolute_brightness(&self) -> f64 {
            self.max_brightness
        }
    }

    fn set_mocks(
        sensor: u16,
        backlight: f64,
    ) -> (Arc<Mutex<impl SensorControl>>, Arc<Mutex<impl BacklightControl>>) {
        let sensor = MockSensor { illuminence: sensor };
        let sensor = Arc::new(Mutex::new(sensor));
        let backlight = MockBacklight { value: backlight, max_brightness: 250.0 };
        let backlight = Arc::new(Mutex::new(backlight));
        (sensor, backlight)
    }

    async fn generate_control_struct() -> Control {
        let (sensor, _backlight) = set_mocks(400, 0.5);
        let set_brightness_abort_handle = None::<AbortHandle>;
        let auto_brightness_abort_handle = None::<AbortHandle>;
        let BrightnessTable { points } = &*BRIGHTNESS_TABLE.lock().await;
        let brightness_table_old = BrightnessTable { points: points.to_vec() };
        let spline_arg = generate_spline(brightness_table_old.clone());

        let current_sender_channel: SenderChannel<f32> = SenderChannel::new();
        let current_sender_channel = Arc::new(Mutex::new(current_sender_channel));

        let auto_sender_channel: SenderChannel<bool> = SenderChannel::new();
        let auto_sender_channel = Arc::new(Mutex::new(auto_sender_channel));

        let offset_sender_channel: SenderChannel<f32> = SenderChannel::new();
        let offset_sender_channel = Arc::new(Mutex::new(offset_sender_channel));
        Control {
            sensor,
            backlight: _backlight,
            set_brightness_abort_handle,
            auto_brightness_abort_handle,
            spline: spline_arg,
            current_sender_channel,
            auto_sender_channel,
            offset_sender_channel,
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

        assert_eq!(cmp_float(0., brightness_curve_lux_to_nits(0, &spline).await), true);
        assert_eq!(cmp_float(0.333, brightness_curve_lux_to_nits(1, &spline).await), true);
        assert_eq!(cmp_float(0.666, brightness_curve_lux_to_nits(2, &spline).await), true);
        assert_eq!(cmp_float(5.75, brightness_curve_lux_to_nits(15, &spline).await), true);
        assert_eq!(cmp_float(6.23, brightness_curve_lux_to_nits(16, &spline).await), true);
        assert_eq!(cmp_float(36.87, brightness_curve_lux_to_nits(100, &spline).await), true);
        assert_eq!(cmp_float(54.12, brightness_curve_lux_to_nits(150, &spline).await), true);
        assert_eq!(cmp_float(102.63, brightness_curve_lux_to_nits(200, &spline).await), true);
        assert_eq!(cmp_float(121.2, brightness_curve_lux_to_nits(240, &spline).await), true);
        assert_eq!(cmp_float(162.96, brightness_curve_lux_to_nits(300, &spline).await), true);
        assert_eq!(cmp_float(300., brightness_curve_lux_to_nits(340, &spline).await), true);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_brightness_curve_after_set_new_brightness_table() {
        let mut control = generate_control_struct().await;
        let points = {
            let BrightnessTable { points } = &*BRIGHTNESS_TABLE.lock().await;
            let mut points_vec = Vec::new();
            for point in points {
                points_vec.push(BrightnessPoint {
                    ambient_lux: point.ambient_lux,
                    display_nits: point.display_nits,
                });
            }
            points_vec
        };
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
        control.set_brightness_table(&brightness_table).await;
        assert_eq!(cmp_float(50.0, brightness_curve_lux_to_nits(0, &control.spline).await), true);
        assert_eq!(cmp_float(50.0, brightness_curve_lux_to_nits(1, &control.spline).await), true);
        assert_eq!(cmp_float(50.0, brightness_curve_lux_to_nits(2, &control.spline).await), true);
        assert_eq!(cmp_float(50.0, brightness_curve_lux_to_nits(15, &control.spline).await), true);
        assert_eq!(cmp_float(50.0, brightness_curve_lux_to_nits(16, &control.spline).await), true);
        assert_eq!(cmp_float(50.0, brightness_curve_lux_to_nits(100, &control.spline).await), true);
        assert_eq!(cmp_float(50.0, brightness_curve_lux_to_nits(150, &control.spline).await), true);
        assert_eq!(cmp_float(50.0, brightness_curve_lux_to_nits(200, &control.spline).await), true);
        assert_eq!(cmp_float(50.0, brightness_curve_lux_to_nits(240, &control.spline).await), true);
        assert_eq!(cmp_float(50.0, brightness_curve_lux_to_nits(300, &control.spline).await), true);
        *BRIGHTNESS_TABLE.lock().await = BrightnessTable { points };
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_brightness_table_after_set_an_offset() {
        let mut control = generate_control_struct().await;
        control.set_auto_brightness_offset(0.3).await;

        assert_eq!(cmp_float(0.0, brightness_curve_lux_to_nits(0, &control.spline).await), true);
        assert_eq!(cmp_float(0.099, brightness_curve_lux_to_nits(1, &control.spline).await), true);
        assert_eq!(cmp_float(0.199, brightness_curve_lux_to_nits(2, &control.spline).await), true);
        assert_eq!(cmp_float(1.725, brightness_curve_lux_to_nits(15, &control.spline).await), true);
        assert_eq!(cmp_float(1.871, brightness_curve_lux_to_nits(16, &control.spline).await), true);
        assert_eq!(
            cmp_float(11.061, brightness_curve_lux_to_nits(100, &control.spline).await),
            true
        );
        assert_eq!(
            cmp_float(16.236, brightness_curve_lux_to_nits(150, &control.spline).await),
            true
        );
        assert_eq!(
            cmp_float(30.791, brightness_curve_lux_to_nits(200, &control.spline).await),
            true
        );
        assert_eq!(
            cmp_float(36.361, brightness_curve_lux_to_nits(240, &control.spline).await),
            true
        );
        assert_eq!(
            cmp_float(48.888, brightness_curve_lux_to_nits(300, &control.spline).await),
            true
        );
        assert_eq!(cmp_float(90., brightness_curve_lux_to_nits(340, &control.spline).await), true);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_brightness_slowly_in_range() {
        let mut result = Vec::new();
        let set_brightness = |nits| {
            result.push(nits as f32);
        };
        set_brightness_slowly(0.4, 0.8, set_brightness, 0.millis()).await;
        assert_eq!(81, result.len(), "wrong length");
        assert_eq!(cmp_float(0.40, result[0]), true);
        assert_eq!(cmp_float(0.41, result[1]), true);
        assert_eq!(cmp_float(0.47, result[15]), true);
        assert_eq!(cmp_float(0.52, result[25]), true);
        assert_eq!(cmp_float(0.57, result[35]), true);
        assert_eq!(cmp_float(0.62, result[45]), true);
        assert_eq!(cmp_float(0.72, result[65]), true);
        assert_eq!(cmp_float(0.75, result[70]), true);
        assert_eq!(cmp_float(0.79, result[79]), true);
        assert_eq!(cmp_float(0.80, result[80]), true);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_brightness_slowly_min() {
        let mut result = Vec::new();
        let set_brightness = |nits| {
            result.push(nits as f32);
        };
        set_brightness_slowly(0.4, 0.0, set_brightness, 0.millis()).await;
        assert_eq!(81, result.len(), "wrong length");
        assert_eq!(cmp_float(0.39, result[0]), true);
        assert_eq!(cmp_float(0.39, result[1]), true);
        assert_eq!(cmp_float(0.32, result[15]), true);
        assert_eq!(cmp_float(0.27, result[25]), true);
        assert_eq!(cmp_float(0.22, result[35]), true);
        assert_eq!(cmp_float(0.17, result[45]), true);
        assert_eq!(cmp_float(0.07, result[65]), true);
        assert_eq!(cmp_float(0.04, result[70]), true);
        assert_eq!(cmp_float(0.0, result[79]), true);
        assert_eq!(cmp_float(0.0, result[80]), true);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_brightness_slowly_max() {
        let mut result = Vec::new();
        let set_brightness = |nits| {
            result.push(nits as f32);
        };
        set_brightness_slowly(0.9, 1.2, set_brightness, 0.millis()).await;
        assert_eq!(cmp_float(21.0, result.len() as f32), true, "wrong length");
        assert_eq!(cmp_float(0.90, result[0]), true);
        assert_eq!(cmp_float(0.91, result[3]), true);
        assert_eq!(cmp_float(0.94, result[9]), true);
        assert_eq!(cmp_float(0.97, result[15]), true);
        assert_eq!(cmp_float(1.0, result[20]), true);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_sensor_and_get_brightness_bright() {
        let BrightnessTable { points } = &*BRIGHTNESS_TABLE.lock().await;
        let brightness_table = BrightnessTable { points: points.to_vec() };
        let spline = generate_spline(brightness_table);
        let (sensor, _backlight) = set_mocks(400, 1.5);
        let value = read_sensor_and_get_brightness(sensor, &spline, 250.0).await;
        assert_eq!(cmp_float(1.2, value), true);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_sensor_and_get_brightness_low_light() {
        let BrightnessTable { points } = &*BRIGHTNESS_TABLE.lock().await;
        let brightness_table = BrightnessTable { points: points.to_vec() };
        let spline = generate_spline(brightness_table);
        let (sensor, _backlight) = set_mocks(0, 0.0);
        let value = read_sensor_and_get_brightness(sensor, &spline, 250.0).await;
        assert_eq!(cmp_float(0.0, value), true);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_brightness_is_abortable_with_auto_brightness_on() {
        let (_sensor, backlight) = set_mocks(0, 0.0);
        let backlight_clone = backlight.clone();
        let abort_handle = set_brightness(0.04, backlight_clone).await;
        // Abort the task before it really gets going
        abort_handle.abort();
        // It should not have reached the final value yet.
        // We know that set_brightness_slowly, at the bottom of the task, finishes at the correct
        // nits value from other tests if it has sufficient time.
        let backlight = backlight.lock().await;

        assert_ne!(cmp_float(0.04, backlight.get_brightness().await.unwrap() as f32), true);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_brightness_is_abortable_with_auto_brightness_off() {
        let (_sensor, backlight) = set_mocks(0, 0.0);
        let backlight_clone = backlight.clone();
        let abort_handle = set_brightness(0.04, backlight_clone).await;
        // Abort the task before it really gets going
        abort_handle.abort();
        // It should not have reached the final value yet.
        // We know that set_brightness_slowly, at the bottom of the task, finishes at the correct
        // nits value from other tests if it has sufficient time.
        let backlight = backlight.lock().await;
        assert_ne!(cmp_float(0.04, backlight.get_brightness().await.unwrap() as f32), true);
    }
}
