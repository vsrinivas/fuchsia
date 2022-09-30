// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use async_trait::async_trait;
use derivative::Derivative;
use fidl::endpoints::ProtocolMarker;
use fidl_fuchsia_hardware_backlight::{
    DeviceMarker as BacklightMarker, DeviceProxy as BacklightProxy, State as BacklightCommand,
};
use fidl_fuchsia_ui_display_internal::{DisplayPowerMarker, DisplayPowerProxy};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_syslog::*;
use fuchsia_zircon as zx;
use futures::{channel::oneshot, lock::Mutex};
use std::sync::Arc;

/// The minimum brightness value that can be sent to the backlight service.
const MIN_REGULATED_BRIGHTNESS: f64 = 0.0004;
/// The maximum brightness that can be sent to the backlight service.
const MAX_REGULATED_BRIGHTNESS: f64 = 1.0;

fn open_backlight() -> Result<BacklightProxy, Error> {
    fx_log_info!("Opening backlight");
    let (proxy, server) = fidl::endpoints::create_proxy::<BacklightMarker>()
        .context("Failed to create backlight proxy")?;
    // TODO(kpt): Don't hardcode this path b/138666351
    fdio::service_connect("/dev/class/backlight/000", server.into_channel())
        .context("Failed to connect built-in service")?;
    Ok(proxy)
}

fn open_display_power_service() -> Result<DisplayPowerProxy, Error> {
    fx_log_info!("Opening display controller");
    connect_to_protocol::<DisplayPowerMarker>()
        .context("Failed to connect to display power service")
}

/// Possible combinations of backlight and display power states.
///
/// When powering down, the backlight must always be turned off with a delay before the DDIC is
/// turned off.
///
/// When powering up, the DDIC must always be turned on with a delay before the backlight is turned
/// on.
#[derive(Derivative)]
#[derivative(Debug)]
enum PowerState {
    /// This state should only be used as a temporary placeholder while swapping values inside
    /// containers (e.g. Mutex).
    Indeterminate,
    BothOn,
    /// The backlight is off and the DDIC is scheduled to turn off.
    BacklightOffDisplayPoweringDown(#[derivative(Debug = "ignore")] fasync::Task<()>),
    BothOff,
    /// The DDIC is on and the backlight is scheduled to turn on. A sequence of backlight changes
    /// may be queued.
    DisplayOnBacklightPoweringUp(
        #[derivative(Debug = "ignore")] fasync::Task<()>,
        Vec<PendingBacklightCommand>,
    ),
}

impl Default for PowerState {
    fn default() -> Self {
        Self::Indeterminate
    }
}

/// A backlight command that is queued to be invoked after the power on delay elapses.
#[derive(Debug)]
struct PendingBacklightCommand {
    command: BacklightCommand,
    /// Will be resolved when the command is invoked (or cancelled if the command is dropped due to
    /// a state change).
    future_handle: oneshot::Sender<Result<(), Error>>,
}

#[derive(Debug, Clone)]
pub struct Backlight {
    backlight_proxy: BacklightProxy,
    display_power: Option<DisplayPower>,
}

impl Backlight {
    /// Creates a simple `Backlight` control, for devices on which DDIC power cannot be switched
    /// off and on.
    pub fn without_display_power() -> Result<Self, Error> {
        let backlight_proxy = open_backlight()?;
        Self::without_display_power_internal(backlight_proxy)
    }

    fn without_display_power_internal(backlight_proxy: BacklightProxy) -> Result<Self, Error> {
        Ok(Backlight { backlight_proxy, display_power: None })
    }

    /// Creates a `Backlight` control that manages both the backlight brightness/power and the power
    /// state of the DDIC.
    #[allow(unused)]
    pub async fn with_display_power(
        power_off_delay_millis: u16,
        power_on_delay_millis: u16,
    ) -> Result<Self, Error> {
        let backlight_proxy = open_backlight()?;
        let display_power_proxy = open_display_power_service()?;
        Self::with_display_power_internal(
            backlight_proxy,
            display_power_proxy,
            zx::Duration::from_millis(power_off_delay_millis as i64),
            zx::Duration::from_millis(power_on_delay_millis as i64),
        )
        .await
    }

    async fn with_display_power_internal(
        backlight_proxy: BacklightProxy,
        display_power_proxy: DisplayPowerProxy,
        power_off_delay: impl Into<zx::Duration>,
        power_on_delay: impl Into<zx::Duration>,
    ) -> Result<Self, Error> {
        let display_power = DisplayPower::new(
            &backlight_proxy,
            display_power_proxy,
            power_off_delay,
            power_on_delay,
        )
        .await?;
        Ok(Backlight { backlight_proxy, display_power: Some(display_power) })
    }

    pub async fn get_max_absolute_brightness(&self) -> Result<f64, Error> {
        let connection = self
            .backlight_proxy
            .get_max_absolute_brightness()
            .await
            .context("Didn't connect correctly")?;
        let max_brightness: f64 = connection
            .map_err(zx::Status::from_raw)
            .context("Failed to get_max_absolute_brightness")?;
        Ok(max_brightness)
    }

    async fn get(&self) -> Result<f64, Error> {
        let backlight_info = get_state_normalized(&self.backlight_proxy).await?;
        assert!(backlight_info.brightness >= 0.0);
        assert!(backlight_info.brightness <= MAX_REGULATED_BRIGHTNESS);
        Ok(if backlight_info.backlight_on { backlight_info.brightness } else { 0.0 })
    }

    async fn set(&self, value: f64) -> Result<(), Error> {
        // TODO(fxbug.dev/36302): Handle error here as well, similar to get_brightness above. Might involve
        let regulated_value =
            num_traits::clamp(value, MIN_REGULATED_BRIGHTNESS, MAX_REGULATED_BRIGHTNESS);
        let backlight_on = value > 0.0;

        match self.display_power.as_ref() {
            None => self.set_backlight_state_normalized(regulated_value, backlight_on).await,
            Some(display_power) => {
                self.clone().set_dual_state(display_power, regulated_value, backlight_on).await
            }
        }
    }

    async fn set_dual_state(
        &self,
        display_power: &DisplayPower,
        regulated_value: f64,
        backlight_on: bool,
    ) -> Result<(), Error> {
        let power_state_arc = display_power.power_state.clone();
        // Note that `power_state` MUST be `drop()`ped before yielding an async value, or there will
        // be a deadlock. Rewriting this `match` expression to not use `.await`, and hence to be
        // able to drop the guard implicitly when it goes out of scope, would be too messy.
        let mut power_state = power_state_arc.lock().await;
        match &mut *power_state {
            PowerState::BothOn => {
                if backlight_on {
                    // See below
                } else {
                    self.set_backlight_state_normalized(regulated_value, backlight_on).await?;
                    fx_log_info!("Turned backlight off");
                    fx_log_info!("DDIC power off scheduled");
                    let task =
                        self.clone().make_scheduled_updates_task(display_power.power_off_delay);
                    *power_state = PowerState::BacklightOffDisplayPoweringDown(task);
                }
                drop(power_state);
                self.set_backlight_state_normalized(regulated_value, backlight_on).await
            }
            PowerState::BacklightOffDisplayPoweringDown(_task) => {
                if backlight_on {
                    fx_log_info!("DDIC power on cancelled");
                    // Cancel the scheduled display shutdown.
                    *power_state = PowerState::BothOn;
                    drop(power_state);
                    self.set_backlight_state_normalized(regulated_value, backlight_on).await
                } else {
                    // No-op. Already scheduled to turn off.
                    drop(power_state);
                    Ok(())
                }
            }
            PowerState::BothOff => {
                if backlight_on {
                    display_power.set_display_power_and_log_errors(true).await?;
                    let (pending_change, receiver) =
                        Self::make_pending_change(regulated_value, backlight_on);
                    let task =
                        self.clone().make_scheduled_updates_task(display_power.power_on_delay);
                    *power_state =
                        PowerState::DisplayOnBacklightPoweringUp(task, vec![pending_change]);
                    drop(power_state);
                    fx_log_info!("Backlight power on scheduled");
                    receiver.await?
                } else {
                    // No-op. Already off.
                    drop(power_state);
                    Ok(())
                }
            }
            PowerState::DisplayOnBacklightPoweringUp(_task, ref mut pending_changes) => {
                if backlight_on {
                    let (pending_change, receiver) =
                        Self::make_pending_change(regulated_value, backlight_on);
                    pending_changes.push(pending_change);
                    drop(power_state);
                    receiver.await?
                } else {
                    fx_log_info!("Backlight power on cancelled");
                    // Cancel scheduled backlight power on.
                    *power_state = PowerState::BothOff;
                    drop(power_state);
                    Ok(())
                }
            }
            PowerState::Indeterminate => {
                unreachable!()
            }
        }
    }

    fn make_scheduled_updates_task(&self, delay: zx::Duration) -> fasync::Task<()> {
        let time = fasync::Time::after(delay);
        fx_log_trace!("Setting timer for {:?}", &time);
        let timer = fasync::Timer::new(time);
        let self_ = self.clone();
        let fut = async move {
            fx_log_trace!("Awaiting timer");
            timer.await;
            fx_log_trace!("Timer {:?} elapsed", time);
            self_.process_scheduled_updates().await;
        };
        fasync::Task::local(fut)
    }

    /// Creates a pending command that can be queued in a
    /// [`PowerState::DisplayOnBacklightPoweringUp`] state.
    fn make_pending_change(
        regulated_value: f64,
        backlight_on: bool,
    ) -> (PendingBacklightCommand, oneshot::Receiver<Result<(), Error>>) {
        let (sender, receiver) = oneshot::channel::<Result<(), Error>>();
        let pending_change = PendingBacklightCommand {
            command: BacklightCommand { backlight_on, brightness: regulated_value },
            future_handle: sender,
        };
        (pending_change, receiver)
    }

    /// Process scheduled updates to the power state.
    async fn process_scheduled_updates(&self) {
        let self_ = self.clone();
        match &self.display_power {
            Some(display_power) => {
                let power_state_arc = display_power.power_state.clone();
                let mut power_state_guard = power_state_arc.lock().await;
                let power_state = std::mem::take(&mut *power_state_guard);

                fx_log_debug!(
                    "Processing scheduled updates after timer. Most recent state: {:?}",
                    &power_state
                );

                match power_state {
                    PowerState::BacklightOffDisplayPoweringDown(_) => {
                        if let Ok(_) = display_power.set_display_power_and_log_errors(false).await {
                            *power_state_guard = PowerState::BothOff;
                        } else {
                            // Don't get stuck in an indeterminate state, nor start a retry loop.
                            // Subsequent calls changes to the backlight state should work normally.
                            *power_state_guard = PowerState::BothOn;
                        }
                    }
                    PowerState::DisplayOnBacklightPoweringUp(_, pending_changes) => {
                        assert!(!pending_changes.is_empty());
                        let mut turned_on = false;
                        for pending_change in pending_changes.into_iter() {
                            assert!(pending_change.command.backlight_on);
                            let result = self_
                                .set_backlight_state_normalized(
                                    pending_change.command.brightness,
                                    pending_change.command.backlight_on,
                                )
                                .await;
                            // Even if a backlight command fails for some reason, we need to treat
                            // the backlight as on. Subsequent commands should still work.
                            *power_state_guard = PowerState::BothOn;
                            fx_log_debug!(
                                "Sending result for pending change {:?}",
                                &pending_change
                            );
                            if let Err(e) = pending_change.future_handle.send(result) {
                                fx_log_warn!("Failed to send result for pending change: {:#?}", e);
                            } else if !turned_on {
                                turned_on = true;
                                fx_log_info!("Turned backlight on");
                            }
                        }
                    }
                    PowerState::Indeterminate => {
                        unreachable!()
                    }
                    _ => {}
                }
            }
            None => {
                unreachable!()
            }
        }
    }

    async fn set_backlight_state_normalized(
        &self,
        regulated_value: f64,
        backlight_on: bool,
    ) -> Result<(), Error> {
        fx_log_debug!(
            "set_state_normalized(brightness: {:.3}, backlight_on: {}",
            regulated_value,
            backlight_on
        );
        self.backlight_proxy
            .set_state_normalized(&mut BacklightCommand {
                backlight_on,
                brightness: regulated_value,
            })
            .await?
            .map_err(|e| zx::Status::from_raw(e))
            .context("Failed to set backlight state")
    }
}

/// Wrapper around [`DisplayPowerProxy`], with state management and configuration values.
#[derive(Debug, Clone)]
struct DisplayPower {
    proxy: DisplayPowerProxy,
    power_state: Arc<Mutex<PowerState>>,
    power_off_delay: zx::Duration,
    power_on_delay: zx::Duration,
}

impl DisplayPower {
    async fn new(
        backlight_proxy: &BacklightProxy,
        display_power_proxy: DisplayPowerProxy,
        power_off_delay: impl Into<zx::Duration>,
        power_on_delay: impl Into<zx::Duration>,
    ) -> Result<Self, Error> {
        // There is no direct way to retrieve the power state of the DDIC, so we infer it from the
        // backlight's state on startup.
        let initial_state = if get_state_normalized(&backlight_proxy).await?.backlight_on {
            PowerState::BothOn
        } else {
            PowerState::BothOff
        };
        fx_log_info!("Initial power state: {:?}", &initial_state);

        Ok(DisplayPower {
            proxy: display_power_proxy,
            power_state: Arc::new(Mutex::new(initial_state)),
            power_off_delay: power_off_delay.into(),
            power_on_delay: power_on_delay.into(),
        })
    }

    async fn set_display_power_and_log_errors(&self, display_on: bool) -> Result<(), Error> {
        let on_off = if display_on { "on" } else { "off" };
        fx_log_info!("Turning DDIC power {}", on_off);
        self.proxy
            .set_display_power(display_on)
            .await
            .map_err(|fidl_error| Into::<Error>::into(fidl_error))
            .with_context(|| format!("Failed to connect to {}", DisplayPowerMarker::DEBUG_NAME))
            .and_then(|inner| {
                inner.map_err(|e| {
                    let status = zx::Status::from_raw(e);
                    Error::from(status)
                })
            })
            .with_context(|| format!("Failed to turn {on_off} display"))
            .map_err(|e| {
                fx_log_err!("{:#?}", &e);
                e
            })?;
        fx_log_info!("Turned DDIC power {}", on_off);
        Ok(())
    }
}

async fn get_state_normalized(backlight_proxy: &BacklightProxy) -> Result<BacklightCommand, Error> {
    backlight_proxy
        .get_state_normalized()
        .await?
        .map_err(|e| zx::Status::from_raw(e))
        .context("Failed to get_state_normalized")
}

#[async_trait]
pub trait BacklightControl: std::fmt::Debug + Send + Sync {
    async fn get_brightness(&self) -> Result<f64, Error>;
    async fn set_brightness(&self, value: f64) -> Result<(), Error>;
    async fn get_max_absolute_brightness(&self) -> Result<f64, Error>;
}

#[async_trait]
impl BacklightControl for Backlight {
    async fn get_brightness(&self) -> Result<f64, Error> {
        self.get().await
    }
    async fn set_brightness(&self, value: f64) -> Result<(), Error> {
        self.clone().set(value).await
    }
    async fn get_max_absolute_brightness(&self) -> Result<f64, Error> {
        self.get_max_absolute_brightness().await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_hardware_backlight::DeviceRequestStream as BacklightRequestStream;
    use fuchsia_async::{self as fasync};
    use futures::{join, prelude::future};
    use futures_util::stream::StreamExt;

    fn mock_backlight() -> (Arc<Backlight>, BacklightRequestStream) {
        let (backlight_proxy, backlight_stream) =
            create_proxy_and_stream::<BacklightMarker>().unwrap();

        (
            Arc::new(Backlight::without_display_power_internal(backlight_proxy).unwrap()),
            backlight_stream,
        )
    }

    async fn mock_device_set(mut reqs: BacklightRequestStream) -> BacklightCommand {
        match reqs.next().await.unwrap() {
            Ok(fidl_fuchsia_hardware_backlight::DeviceRequest::SetStateNormalized {
                state: command,
                ..
            }) => {
                return command;
            }
            request => panic!("Unexpected request: {:?}", request),
        }
    }

    async fn mock_device_get(
        mut reqs: BacklightRequestStream,
        backlight_command: BacklightCommand,
    ) {
        match reqs.next().await.unwrap() {
            Ok(fidl_fuchsia_hardware_backlight::DeviceRequest::GetStateNormalized {
                responder,
            }) => {
                let response = backlight_command;
                let _ = responder.send(&mut Ok(response));
            }
            Ok(fidl_fuchsia_hardware_backlight::DeviceRequest::GetMaxAbsoluteBrightness {
                responder,
            }) => {
                if let Err(e) = responder.send(&mut Ok(250.0)) {
                    panic!("Failed to reply to GetMaxAbsoluteBrightness: {}", e);
                }
            }
            request => panic!("Unexpected request: {:?}", request),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_brightness_returns_zero_if_backlight_off() {
        // Setup
        let (mock, backlight_stream) = mock_backlight();
        let backlight_fut = mock_device_get(
            backlight_stream,
            BacklightCommand { backlight_on: false, brightness: 0.04 },
        );

        // Act
        let get_fut = mock.get();
        let (brightness, _) = future::join(get_fut, backlight_fut).await;

        // Assert
        assert_eq!(brightness.unwrap(), 0.0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_brightness_returns_non_zero_if_backlight_on() {
        // Setup
        let (mock, backlight_stream) = mock_backlight();
        let backlight_fut = mock_device_get(
            backlight_stream,
            BacklightCommand { backlight_on: true, brightness: 0.04 },
        );

        // Act
        let get_fut = mock.get();
        let (brightness, _) = future::join(get_fut, backlight_fut).await;

        // Assert
        assert_eq!(brightness.unwrap(), 0.04);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_zero_brightness_turns_backlight_off() {
        // Setup
        let (mock, backlight_stream) = mock_backlight();
        let backlight_fut = mock_device_set(backlight_stream);

        // Act
        let set_fut = mock.set(0.0);
        let (_, backlight_command) = futures::join!(set_fut, backlight_fut);

        // Assert
        assert_eq!(backlight_command.backlight_on, false);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_negative_brightness_turns_backlight_off() {
        // Setup
        let (mock, backlight_stream) = mock_backlight();
        let backlight_fut = mock_device_set(backlight_stream);

        // Act
        let set_fut = mock.set(-0.01);
        let (_, backlight_command) = join!(set_fut, backlight_fut);

        // Assert
        assert_eq!(backlight_command.backlight_on, false);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_brightness_turns_backlight_on() {
        // Setup
        let (mock, backlight_stream) = mock_backlight();
        let backlight_fut = mock_device_set(backlight_stream);

        // Act
        let set_fut = mock.set(0.55);
        let (_, backlight_command) = join!(set_fut, backlight_fut);

        // Assert
        assert_eq!(backlight_command.backlight_on, true);
        assert_eq!(backlight_command.brightness, 0.55);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_max_absolute_brightness() {
        // Setup
        let (mock, backlight_stream) = mock_backlight();
        let backlight_fut = mock_device_get(
            backlight_stream,
            BacklightCommand { backlight_on: false, brightness: 0.04 },
        );

        // Act
        let mock_fut = mock.get_max_absolute_brightness();
        let (max_brightness, _) = future::join(mock_fut, backlight_fut).await;

        // Assert
        assert_eq!(max_brightness.unwrap(), 250.0);
    }
}

#[cfg(test)]
mod dual_state_tests {
    use super::*;
    use assert_matches::assert_matches;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_hardware_backlight::DeviceRequestStream as BacklightRequestStream;
    use fidl_fuchsia_ui_display_internal::{DisplayPowerRequest, DisplayPowerRequestStream};
    use fuchsia_async::{self as fasync, Task};
    use fuchsia_syslog::fx_log_warn;
    use futures::{prelude::future, Future, TryStreamExt};
    use std::task::Poll;
    use test_helpers::ResettableFuture;

    #[derive(Debug, Clone)]
    struct FakeBacklightService {
        get_state_normalized_response: ResettableFuture<Result<BacklightCommand, i32>>,
        set_state_normalized_response: Arc<Mutex<Result<(), i32>>>,
    }

    #[allow(dead_code)]
    impl FakeBacklightService {
        pub fn new() -> Self {
            Self {
                get_state_normalized_response: ResettableFuture::new(),
                set_state_normalized_response: Arc::new(Mutex::new(Ok(()))),
            }
        }

        pub fn start(&self) -> Result<(BacklightProxy, Task<()>), Error> {
            let (proxy, stream) = create_proxy_and_stream::<BacklightMarker>()?;
            let task = Task::local(self.clone().process_requests(stream));
            Ok((proxy, task))
        }

        async fn process_requests(self, mut stream: BacklightRequestStream) {
            use fidl_fuchsia_hardware_backlight::DeviceRequest::*;

            fx_log_debug!("FakeBacklightService::process_requests");
            while let Ok(Some(req)) = stream.try_next().await {
                fx_log_debug!("FakeBacklightService: {}", req.method_name());
                match req {
                    GetStateNormalized { responder } => {
                        let mut result = self.get_state_normalized_response.get().await;
                        responder.send(&mut result).expect("send GetStateNormalized");
                    }
                    SetStateNormalized { state, responder } => {
                        let mut result = self.set_state_normalized_response.lock().await.clone();
                        if result.is_ok() {
                            self.set_get_state_normalized_response(Ok(state)).await;
                        }
                        responder.send(&mut result).expect("send SetStateNormalized");
                    }
                    _ => {
                        unimplemented!();
                    }
                };
            }
        }

        pub async fn set_get_state_normalized_response(
            &self,
            response: Result<BacklightCommand, i32>,
        ) {
            self.get_state_normalized_response.set(response).await;
        }

        pub async fn clear_get_state_normalized_response(&self) {
            self.get_state_normalized_response.clear().await;
        }

        pub async fn set_set_state_normalized_response(&self, result: Result<(), i32>) {
            let mut guard = self.set_state_normalized_response.lock().await;
            *guard = result;
        }
    }

    #[derive(Debug, Clone)]
    struct FakeDisplayPowerService {
        set_display_power_response: Arc<Mutex<Result<(), i32>>>,
        last_set_display_power_value: Arc<Mutex<Option<bool>>>,
    }

    #[allow(dead_code)]
    impl FakeDisplayPowerService {
        pub fn new() -> Self {
            Self {
                set_display_power_response: Arc::new(Mutex::new(Ok(()))),
                last_set_display_power_value: Arc::new(Mutex::new(None)),
            }
        }

        pub fn start(&self) -> Result<(DisplayPowerProxy, Task<()>), Error> {
            let (proxy, stream) = create_proxy_and_stream::<DisplayPowerMarker>()?;
            let task = Task::local(self.clone().process_requests(stream));
            Ok((proxy, task))
        }

        async fn process_requests(self, mut stream: DisplayPowerRequestStream) {
            fx_log_debug!("FakeDisplayPowerService::process_requests");
            while let Ok(Some(req)) = stream.try_next().await {
                fx_log_debug!("FakeDisplayPowerService: {}", req.method_name());
                match req {
                    DisplayPowerRequest::SetDisplayPower { power_on, responder } => {
                        let mut result = self.set_display_power_response.lock().await.clone();
                        if result.is_ok() {
                            self.last_set_display_power_value.lock().await.replace(power_on);
                        }
                        responder.send(&mut result).expect("send SetDisplayPower");
                    }
                };
            }
            fx_log_warn!("FakeDisplayPowerService stopped");
        }

        pub async fn set_set_display_power_response(&self, response: Result<(), i32>) {
            (*self.set_display_power_response.lock().await) = response;
        }

        pub async fn last_set_display_power_value(&self) -> Option<bool> {
            self.last_set_display_power_value.lock().await.clone()
        }
    }

    trait PollExt<T> {
        fn into_option(self) -> Option<T>;
        fn unwrap(self) -> T;
    }

    impl<T> PollExt<T> for Poll<T> {
        fn into_option(self) -> Option<T> {
            match self {
                Poll::Ready(x) => Some(x),
                Poll::Pending => None,
            }
        }

        fn unwrap(self) -> T {
            self.into_option().unwrap()
        }
    }

    trait TestExecutorExt {
        fn pin_and_run_until_stalled<F: Future>(&mut self, main_future: F) -> Option<F::Output>;
        /// Wakes expired timers and runs any existing spawned tasks until they stall. Returns
        /// `true` if one or more timers were awoken.
        fn wake_timers_and_run_until_stalled(&mut self) -> bool;
    }

    impl TestExecutorExt for fasync::TestExecutor {
        fn pin_and_run_until_stalled<F: Future>(&mut self, main_future: F) -> Option<F::Output> {
            self.run_until_stalled(&mut Box::pin(main_future)).into_option()
        }

        fn wake_timers_and_run_until_stalled(&mut self) -> bool {
            let did_wake_timers = self.wake_expired_timers();
            let _ = self.run_until_stalled(&mut future::pending::<()>());
            did_wake_timers
        }
    }

    #[allow(dead_code)]
    struct Handles {
        fake_backlight_service: FakeBacklightService,
        backlight_proxy: BacklightProxy,
        backlight_task: Task<()>,

        fake_display_power_service: FakeDisplayPowerService,
        display_power_proxy: DisplayPowerProxy,
        display_power_task: Task<()>,

        backlight: Backlight,
    }

    impl Handles {
        /// Note that callers need to declare a variable for the executor _before_ the handles, or
        /// else the executor will cause a panic when it's dropped before its futures.
        fn new(
            power_off_delay_ms: i64,
            power_on_delay_ms: i64,
            initial_backlight_state: BacklightCommand,
        ) -> (fasync::TestExecutor, Handles) {
            let mut exec = fasync::TestExecutor::new_with_fake_time().unwrap();
            exec.set_fake_time(zx::Time::ZERO.into());

            let fake_backlight_service = FakeBacklightService::new();
            let (backlight_proxy, backlight_task) = fake_backlight_service.start().unwrap();

            let fake_display_power_service = FakeDisplayPowerService::new();
            let (display_power_proxy, display_power_task) =
                fake_display_power_service.start().unwrap();

            let fake_backlight_service_ = fake_backlight_service.clone();

            let backlight = exec
                .pin_and_run_until_stalled(async {
                    fake_backlight_service_
                        .set_get_state_normalized_response(Ok(initial_backlight_state))
                        .await;

                    Backlight::with_display_power_internal(
                        backlight_proxy.clone(),
                        display_power_proxy.clone(),
                        zx::Duration::from_millis(power_off_delay_ms),
                        zx::Duration::from_millis(power_on_delay_ms),
                    )
                    .await
                    .unwrap()
                })
                .unwrap();

            (
                exec,
                Handles {
                    fake_backlight_service,
                    backlight_proxy,
                    backlight_task,
                    fake_display_power_service,
                    display_power_proxy,
                    display_power_task,
                    backlight,
                },
            )
        }
    }

    #[test]
    fn positive_brightness_changes_without_affecting_ddic() {
        let power_off_delay_ms = 100;
        let power_on_delay_ms = 50;

        let (mut exec, h) = Handles::new(
            power_off_delay_ms,
            power_on_delay_ms,
            BacklightCommand { backlight_on: true, brightness: 1.0 },
        );

        exec.pin_and_run_until_stalled(async {
            assert_eq!(h.backlight.get().await.unwrap(), 1.0);

            h.backlight.set(0.9).await.unwrap();
            assert_eq!(h.backlight.get().await.unwrap(), 0.9);

            h.backlight.set(0.5).await.unwrap();
            assert_eq!(h.backlight.get().await.unwrap(), 0.5);

            assert_eq!(h.fake_display_power_service.last_set_display_power_value().await, None)
        })
        .unwrap();
    }

    #[test]
    fn zero_brightness_turns_ddic_off() {
        let power_off_delay_ms = 100;
        let power_on_delay_ms = 50;

        let (mut exec, h) = Handles::new(
            power_off_delay_ms,
            power_on_delay_ms,
            BacklightCommand { backlight_on: true, brightness: 1.0 },
        );

        exec.pin_and_run_until_stalled(async {
            assert_eq!(h.backlight.get().await.unwrap(), 1.0);
            assert_eq!(h.fake_display_power_service.last_set_display_power_value().await, None);

            h.backlight.set(0.0).await.unwrap();
            assert_eq!(h.backlight.get().await.unwrap(), 0.0);
            assert_eq!(h.fake_display_power_service.last_set_display_power_value().await, None);
        })
        .unwrap();

        // Right before the power-off delay
        exec.set_fake_time(
            (zx::Time::ZERO + zx::Duration::from_millis(power_off_delay_ms - 1)).into(),
        );
        assert_eq!(exec.wake_timers_and_run_until_stalled(), false);

        exec.pin_and_run_until_stalled(async {
            assert_eq!(h.fake_display_power_service.last_set_display_power_value().await, None);
        })
        .unwrap();

        // Right after the power-off delay.
        exec.set_fake_time(
            (zx::Time::ZERO + zx::Duration::from_millis(power_off_delay_ms + 1)).into(),
        );
        assert_eq!(exec.wake_timers_and_run_until_stalled(), true);

        exec.pin_and_run_until_stalled(async {
            assert_eq!(
                h.fake_display_power_service.last_set_display_power_value().await,
                Some(false)
            );
        })
        .unwrap();
    }

    #[test]
    fn backlight_turns_on_after_ddic() {
        let power_off_delay_ms = 100;
        let power_on_delay_ms = 50;

        let (mut exec, h) = Handles::new(
            power_off_delay_ms,
            power_on_delay_ms,
            BacklightCommand { backlight_on: false, brightness: MIN_REGULATED_BRIGHTNESS },
        );

        exec.pin_and_run_until_stalled(async {
            assert_eq!(h.backlight.get().await.unwrap(), 0.0);
            assert_eq!(h.fake_display_power_service.last_set_display_power_value().await, None);
        })
        .unwrap();

        let backlight_ = h.backlight.clone();

        let mut turn_on_backlight_fut = Box::pin(async {
            backlight_.set(0.1).await.unwrap();
        });
        assert_matches!(exec.run_until_stalled(&mut turn_on_backlight_fut), Poll::Pending);

        exec.pin_and_run_until_stalled(async {
            assert_eq!(h.backlight.get().await.unwrap(), 0.0);
            assert_eq!(
                h.fake_display_power_service.last_set_display_power_value().await,
                Some(true)
            );
        })
        .unwrap();

        exec.set_fake_time(
            (zx::Time::ZERO + zx::Duration::from_millis(power_on_delay_ms - 1)).into(),
        );
        assert_eq!(exec.wake_timers_and_run_until_stalled(), false);
        assert_matches!(exec.run_until_stalled(&mut turn_on_backlight_fut), Poll::Pending);
        exec.pin_and_run_until_stalled(async {
            assert_eq!(h.backlight.get().await.unwrap(), 0.0);
        })
        .unwrap();

        exec.set_fake_time(
            (zx::Time::ZERO + zx::Duration::from_millis(power_on_delay_ms + 1)).into(),
        );
        assert_eq!(exec.wake_timers_and_run_until_stalled(), true);
        assert_matches!(exec.run_until_stalled(&mut turn_on_backlight_fut), Poll::Ready(()));
        exec.pin_and_run_until_stalled(async {
            assert_eq!(h.backlight.get().await.unwrap(), 0.1);
        })
        .unwrap();
    }

    #[test]
    fn repeated_backlight_off_commands_do_not_affect_ddic() {
        let power_off_delay_ms = 100;
        let power_on_delay_ms = 50;

        let (mut exec, h) = Handles::new(
            power_off_delay_ms,
            power_on_delay_ms,
            BacklightCommand { backlight_on: false, brightness: MIN_REGULATED_BRIGHTNESS },
        );

        exec.pin_and_run_until_stalled(async {
            assert_eq!(h.backlight.get().await.unwrap(), 0.0);
            assert_eq!(h.fake_display_power_service.last_set_display_power_value().await, None);

            h.backlight.set(0.0).await.unwrap();
            assert_eq!(h.backlight.get().await.unwrap(), 0.0);
            assert_eq!(h.fake_display_power_service.last_set_display_power_value().await, None);
        })
        .unwrap();
    }

    #[test]
    fn ddic_power_off_is_preempted_by_backlight_on_commands() {
        let power_off_delay_ms = 100;
        let power_on_delay_ms = 50;

        let (mut exec, h) = Handles::new(
            power_off_delay_ms,
            power_on_delay_ms,
            BacklightCommand { backlight_on: true, brightness: 1.0 },
        );

        exec.pin_and_run_until_stalled(async {
            h.backlight.set(0.0).await.unwrap();
            assert_eq!(h.backlight.get().await.unwrap(), 0.0);
            assert_eq!(h.fake_display_power_service.last_set_display_power_value().await, None);
        })
        .unwrap();

        // Right before the power-off delay
        exec.set_fake_time(
            (zx::Time::ZERO + zx::Duration::from_millis(power_off_delay_ms - 10)).into(),
        );
        assert_eq!(exec.wake_timers_and_run_until_stalled(), false);

        exec.pin_and_run_until_stalled(async {
            assert_eq!(h.fake_display_power_service.last_set_display_power_value().await, None);
        })
        .unwrap();

        exec.pin_and_run_until_stalled(async {
            h.backlight.set(0.5).await.unwrap();
            assert_eq!(h.backlight.get().await.unwrap(), 0.5);
            assert_eq!(h.fake_display_power_service.last_set_display_power_value().await, None);
        })
        .unwrap();

        // Right after the power-off delay.
        exec.set_fake_time(
            (zx::Time::ZERO + zx::Duration::from_millis(power_off_delay_ms + 10)).into(),
        );
        // The timer task should have been canceled (dropped).
        assert_eq!(exec.wake_timers_and_run_until_stalled(), false);

        exec.pin_and_run_until_stalled(async {
            assert_eq!(h.fake_display_power_service.last_set_display_power_value().await, None);
        })
        .unwrap();
    }

    #[test]
    fn backlight_power_on_is_preempted_by_ddic_off_commands() {
        let power_off_delay_ms = 100;
        let power_on_delay_ms = 50;

        let (mut exec, h) = Handles::new(
            power_off_delay_ms,
            power_on_delay_ms,
            BacklightCommand { backlight_on: false, brightness: MIN_REGULATED_BRIGHTNESS },
        );

        let mut turn_on_backlight_1_fut = Box::pin(h.backlight.set(0.1));
        let mut turn_on_backlight_2_fut = Box::pin(h.backlight.set(0.2));
        assert_matches!(exec.run_until_stalled(&mut turn_on_backlight_1_fut), Poll::Pending);
        assert_matches!(exec.run_until_stalled(&mut turn_on_backlight_2_fut), Poll::Pending);

        exec.set_fake_time(
            (zx::Time::ZERO + zx::Duration::from_millis(power_on_delay_ms - 1)).into(),
        );
        assert_eq!(exec.wake_timers_and_run_until_stalled(), false);
        assert_matches!(exec.run_until_stalled(&mut turn_on_backlight_1_fut), Poll::Pending);
        assert_matches!(exec.run_until_stalled(&mut turn_on_backlight_2_fut), Poll::Pending);

        let turn_off_backlight_fut = Box::pin(h.backlight.set(0.0));
        exec.pin_and_run_until_stalled(async {
            assert_matches!(turn_off_backlight_fut.await, Ok(()));
            // The futures that would have turned on the backlight should be cancelled.
            assert_matches!(
                turn_on_backlight_1_fut.await.unwrap_err().downcast::<oneshot::Canceled>(),
                Ok(_)
            );
            assert_matches!(
                turn_on_backlight_2_fut.await.unwrap_err().downcast::<oneshot::Canceled>(),
                Ok(_)
            );

            assert_eq!(h.backlight.get().await.unwrap(), 0.0);
        })
        .unwrap();

        exec.set_fake_time(
            (zx::Time::ZERO + zx::Duration::from_millis(power_on_delay_ms + 1)).into(),
        );
        assert_eq!(exec.wake_timers_and_run_until_stalled(), false);

        exec.pin_and_run_until_stalled(async {
            assert_eq!(h.backlight.get().await.unwrap(), 0.0);
        })
        .unwrap();
    }

    #[test]
    fn error_in_ddic_power_off_does_not_affect_later_backlight_commands() {
        let power_off_delay_ms = 100;
        let power_on_delay_ms = 50;

        let (mut exec, h) = Handles::new(
            power_off_delay_ms,
            power_on_delay_ms,
            BacklightCommand { backlight_on: true, brightness: 1.0 },
        );

        exec.pin_and_run_until_stalled(async {
            h.fake_display_power_service
                .set_set_display_power_response(Err(zx::Status::BAD_STATE.into_raw()))
                .await;

            assert_eq!(h.backlight.get().await.unwrap(), 1.0);
            assert_eq!(h.fake_display_power_service.last_set_display_power_value().await, None);

            h.backlight.set(0.0).await.unwrap();
            assert_eq!(h.backlight.get().await.unwrap(), 0.0);
            assert_eq!(h.fake_display_power_service.last_set_display_power_value().await, None);
        })
        .unwrap();

        exec.set_fake_time(
            (zx::Time::ZERO + zx::Duration::from_millis(power_off_delay_ms + 1)).into(),
        );
        assert_eq!(exec.wake_timers_and_run_until_stalled(), true);

        exec.pin_and_run_until_stalled(async {
            assert_eq!(h.fake_display_power_service.last_set_display_power_value().await, None);

            h.backlight.set(0.5).await.unwrap();
            assert_eq!(h.backlight.get().await.unwrap(), 0.5);
            assert_eq!(h.fake_display_power_service.last_set_display_power_value().await, None);
        })
        .unwrap();
    }

    #[test]
    fn error_in_ddic_power_on_is_recoverable() {
        let power_off_delay_ms = 100;
        let power_on_delay_ms = 50;

        let (mut exec, h) = Handles::new(
            power_off_delay_ms,
            power_on_delay_ms,
            BacklightCommand { backlight_on: false, brightness: MIN_REGULATED_BRIGHTNESS },
        );

        exec.pin_and_run_until_stalled(async {
            h.fake_display_power_service
                .set_set_display_power_response(Err(zx::Status::UNAVAILABLE.into_raw()))
                .await;

            assert_matches!(h.backlight.set(0.5).await, Err(_));
            assert_eq!(h.fake_display_power_service.last_set_display_power_value().await, None);

            h.fake_display_power_service.set_set_display_power_response(Ok(())).await;
        })
        .unwrap();

        let mut retry_fut = Box::pin(h.backlight.set(0.7));
        assert_matches!(exec.run_until_stalled(&mut retry_fut), Poll::Pending);

        exec.set_fake_time(
            (zx::Time::ZERO + zx::Duration::from_millis(power_on_delay_ms + 1)).into(),
        );
        assert_eq!(exec.wake_timers_and_run_until_stalled(), true);

        assert_matches!(exec.run_until_stalled(&mut retry_fut), Poll::Ready(Ok(())));
    }

    #[test]
    fn ddic_does_not_power_off_if_backlight_fails_to_power_off() {
        let power_off_delay_ms = 100;
        let power_on_delay_ms = 50;

        let (mut exec, h) = Handles::new(
            power_off_delay_ms,
            power_on_delay_ms,
            BacklightCommand { backlight_on: true, brightness: 0.5 },
        );

        exec.pin_and_run_until_stalled(async {
            h.fake_backlight_service
                .set_set_state_normalized_response(Err(zx::Status::NO_RESOURCES.into_raw()))
                .await;
            assert_matches!(h.backlight.set(0.0).await, Err(_));
        })
        .unwrap();

        exec.set_fake_time(
            (zx::Time::ZERO + zx::Duration::from_millis(power_off_delay_ms + 1)).into(),
        );
        assert_eq!(exec.wake_timers_and_run_until_stalled(), false);

        exec.pin_and_run_until_stalled(async {
            assert_eq!(h.fake_display_power_service.last_set_display_power_value().await, None);
        })
        .unwrap();
    }
}
