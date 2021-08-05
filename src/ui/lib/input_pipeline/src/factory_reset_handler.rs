// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::consumer_controls::ConsumerControlsEvent,
    crate::input_device,
    crate::input_handler::InputHandler,
    anyhow::{anyhow, Context as _, Error},
    async_trait::async_trait,
    async_utils::hanging_get::server::HangingGet,
    fidl_fuchsia_input_report as fidl_input_report,
    fidl_fuchsia_media::AudioRenderUsage,
    fidl_fuchsia_media_sounds::PlayerMarker,
    fidl_fuchsia_recovery::FactoryResetMarker,
    fidl_fuchsia_recovery_policy::{DeviceRequest, DeviceRequestStream},
    fidl_fuchsia_recovery_ui::{
        FactoryResetCountdownRequestStream, FactoryResetCountdownState,
        FactoryResetCountdownWatchResponder,
    },
    fuchsia_async::{Duration, Task, Time, Timer},
    fuchsia_component,
    fuchsia_zircon::Channel,
    futures::StreamExt,
    std::{
        cell::RefCell,
        fs::{self, File},
        path::Path,
        rc::Rc,
    },
};

/// FacotryResetState tracks the state of the device through the factory reset
/// process.
///
/// # Values
/// ## Disallowed
/// Factory reset of the device is not allowed. This is used to
/// keep public devices from being reset, such as when being used in kiosk mode.
///
/// ### Transitions
/// Disallowed → Idle
///
/// ## Idle
/// This is the default state for a device when factory resets are allowed but
/// is not currently being reset.
///
/// ### Transitions
/// Idle → Disallowed
/// Idle → ButtonCountdown
///
/// ## ButtonCountdown
/// This state represents the fact that the reset button has been pressed and a
/// countdown has started to verify that the button was pressed intentionally.
///
/// ### Transitions
/// ButtonCountdown → Disallowed
/// ButtonCountdown → Idle
/// ButtonCountdown → ResetCountdown
///
/// ## ResetCountdown
/// The button countdown has completed indicating that this was a purposeful
/// action so a reset countdown is started to give the user a chance to cancel
/// the factory reset.
///
/// ### Transitions
/// ResetCountdown → Disallowed
/// ResetCountdown → Idle
/// ResetCountdown → Resetting
///
/// ## Resetting
/// Once the device is in this state a factory reset is imminent and can no
/// longer be cancelled.
#[derive(Clone, Copy, Debug, PartialEq)]
enum FactoryResetState {
    Disallowed,
    Idle,
    ButtonCountdown { deadline: Time },
    ResetCountdown { deadline: Time },
    Resetting,
}

const FACTORY_RESET_DISALLOWED_PATH: &'static str = "/data/factory_reset_disallowed";
const FACTORY_RESET_SOUND_PATH: &'static str = "/config/data/chirp-start-tone.wav";

const BUTTON_TIMEOUT: Duration = Duration::from_millis(500);
const RESET_TIMEOUT: Duration = Duration::from_seconds(10);

type NotifyFn = Box<dyn Fn(&FactoryResetState, FactoryResetCountdownWatchResponder) -> bool + Send>;
type ResetCountdownHangingGet =
    HangingGet<FactoryResetState, FactoryResetCountdownWatchResponder, NotifyFn>;

/// A [`FactoryResetHandler`] tracks the state of the consumer control buttons
/// and starts the factory reset process after appropriate timeouts.
pub struct FactoryResetHandler {
    factory_reset_state: RefCell<FactoryResetState>,
    countdown_hanging_get: RefCell<ResetCountdownHangingGet>,
}

/// Uses the `ConsumerControlsEvent` to determine whether the device should
/// start the Factory Reset process. The driver will turn special button
/// combinations into a `FactoryReset` signal so this code only needs to
/// listen for that.
fn is_reset_requested(event: &ConsumerControlsEvent) -> bool {
    event.pressed_buttons.iter().any(|button| match button {
        fidl_input_report::ConsumerControlButton::FactoryReset => true,
        _ => false,
    })
}

impl FactoryResetHandler {
    /// Creates a new [`FactoryResetHandler`] that listens for the reset button
    /// and handles timing down and, ultimately, factory resetting the device.
    pub fn new() -> Rc<Self> {
        let initial_state = if Path::new(FACTORY_RESET_DISALLOWED_PATH).exists() {
            FactoryResetState::Disallowed
        } else {
            FactoryResetState::Idle
        };

        let countdown_hanging_get = FactoryResetHandler::init_hanging_get(initial_state);

        Rc::new(Self {
            factory_reset_state: RefCell::new(initial_state),
            countdown_hanging_get: RefCell::new(countdown_hanging_get),
        })
    }

    /// Handles the request stream for FactoryResetCountdown
    ///
    /// # Parameters
    /// `stream`: The `FactoryResetCountdownRequestStream` to be handled.
    pub fn handle_factory_reset_countdown_request_stream(
        self: Rc<Self>,
        mut stream: FactoryResetCountdownRequestStream,
    ) -> impl futures::Future<Output = Result<(), Error>> {
        let subscriber = self.countdown_hanging_get.borrow_mut().new_subscriber();

        async move {
            while let Some(request_result) = stream.next().await {
                let watcher = request_result?
                    .into_watch()
                    .ok_or(anyhow!("Failed to get FactoryResetCoundown Watcher"))?;
                subscriber.register(watcher)?;
            }

            Ok(())
        }
    }

    /// Handles the request stream for fuchsia.recovery.policy.Device
    ///
    /// # Parameters
    /// `stream`: The `DeviceRequestStream` to be handled.
    pub fn handle_recovery_policy_device_request_stream(
        self: Rc<Self>,
        mut stream: DeviceRequestStream,
    ) -> impl futures::Future<Output = Result<(), Error>> {
        async move {
            while let Some(request_result) = stream.next().await {
                let DeviceRequest::SetIsLocalResetAllowed { allowed, .. } = request_result?;
                match self.factory_reset_state() {
                    FactoryResetState::Disallowed if allowed => {
                        // Update state and delete file
                        self.set_factory_reset_state(FactoryResetState::Idle);
                        fs::remove_file(FACTORY_RESET_DISALLOWED_PATH).map_err(|error| {
                            anyhow!("Failed to SetIsLocalResetAllowed to true: {:?}", error)
                        })?;
                    }
                    _ if !allowed => {
                        // Update state and create file
                        self.set_factory_reset_state(FactoryResetState::Disallowed);
                        File::create(FACTORY_RESET_DISALLOWED_PATH).map_err(|error| {
                            anyhow!("Failed to SetIsLocalResetAllowed to false: {:?}", error)
                        })?;
                    }
                    _ => (),
                }
            }

            Ok(())
        }
    }

    /// Handles `ConsumerControlEvent`s when the device is in the
    /// `FactoryResetState::Idle` state
    async fn handle_allowed_event(self: &Rc<Self>, event: &ConsumerControlsEvent) {
        if is_reset_requested(event) {
            if let Err(error) = self.start_button_countdown().await {
                tracing::error!("Failed to factory reset device: {:?}", error);
            }
        }
    }

    /// Handles `ConsumerControlEvent`s when the device is in the
    /// `FactoryResetState::Disallowed` state
    fn handle_disallowed_event(self: &Rc<Self>, event: &ConsumerControlsEvent) {
        if is_reset_requested(event) {
            tracing::error!("Attempted to factory reset a device that is not allowed to reset");
        }
    }

    /// Handles `ConsumerControlEvent`s when the device is in the
    /// `FactoryResetState::ButtonCountdown` state
    fn handle_button_countdown_event(self: &Rc<Self>, event: &ConsumerControlsEvent) {
        if !is_reset_requested(event) {
            // Cancel button timeout
            self.set_factory_reset_state(FactoryResetState::Idle);
        }
    }

    /// Handles `ConsumerControlEvent`s when the device is in the
    /// `FactoryResetState::ResetCountdown` state
    fn handle_reset_countdown_event(self: &Rc<Self>, event: &ConsumerControlsEvent) {
        if !is_reset_requested(event) {
            // Cancel reset timeout
            self.set_factory_reset_state(FactoryResetState::Idle);
        }
    }

    fn init_hanging_get(initial_state: FactoryResetState) -> ResetCountdownHangingGet {
        let notify_fn: NotifyFn = Box::new(|state, responder| {
            let deadline = match state {
                FactoryResetState::ResetCountdown { deadline } => {
                    Some(deadline.into_nanos() as i64)
                }
                _ => None,
            };

            let countdown_state = FactoryResetCountdownState {
                scheduled_reset_time: deadline,
                ..FactoryResetCountdownState::EMPTY
            };

            if responder.send(countdown_state).is_err() {
                tracing::error!("Failed to send factory reset countdown state");
            }

            true
        });

        ResetCountdownHangingGet::new(initial_state, notify_fn)
    }

    /// Sets the state of FactoryResetHandler and notifies watchers of the updated state.
    fn set_factory_reset_state(self: &Rc<Self>, state: FactoryResetState) {
        *self.factory_reset_state.borrow_mut() = state;
        self.countdown_hanging_get.borrow_mut().new_publisher().set(state);
    }

    fn factory_reset_state(self: &Rc<Self>) -> FactoryResetState {
        *self.factory_reset_state.borrow()
    }

    /// Handles waiting for the reset button to be held down long enough to start
    /// the factory reset countdown.
    async fn start_button_countdown(self: &Rc<Self>) -> Result<(), Error> {
        let deadline = Time::after(BUTTON_TIMEOUT);
        self.set_factory_reset_state(FactoryResetState::ButtonCountdown { deadline });

        // Wait for button timeout
        Timer::new(Time::after(BUTTON_TIMEOUT)).await;

        // Make sure the buttons are still held
        if let FactoryResetState::ButtonCountdown { deadline: state_deadline } =
            self.factory_reset_state()
        {
            if state_deadline == deadline {
                self.start_reset_countdown().await?;
            }
        }

        Ok(())
    }

    /// Handles waiting for the reset countdown to complete before resetting the
    /// device.
    async fn start_reset_countdown(self: &Rc<Self>) -> Result<(), Error> {
        let deadline = Time::after(RESET_TIMEOUT);
        self.set_factory_reset_state(FactoryResetState::ResetCountdown { deadline });

        // Wait for reset timeout
        Timer::new(Time::after(RESET_TIMEOUT)).await;

        // Make sure the buttons are still held
        if let FactoryResetState::ResetCountdown { deadline: state_deadline } =
            self.factory_reset_state()
        {
            if state_deadline == deadline {
                self.reset().await?;
            }
        }

        Ok(())
    }

    /// Retrieves and plays the sound associated with factory resetting the device.
    async fn play_reset_sound(self: &Rc<Self>) -> Result<(), Error> {
        // Get sound
        let sound_file = File::open(FACTORY_RESET_SOUND_PATH)
            .context("Failed to open factory reset sound file")?;
        let sound_channel = Channel::from(fdio::transfer_fd(sound_file)?);
        let sound_endpoint =
            fidl::endpoints::ClientEnd::<fidl_fuchsia_io::FileMarker>::new(sound_channel);

        // Play sound
        let sound_player = fuchsia_component::client::connect_to_protocol::<PlayerMarker>()?;

        let sound_id = 0;
        let _duration = sound_player
            .add_sound_from_file(sound_id, sound_endpoint)
            .await?
            .map_err(|status| anyhow::format_err!("AddSoundFromFile failed {}", status))?;

        sound_player
            .play_sound(sound_id, AudioRenderUsage::Media)
            .await?
            .map_err(|err| anyhow::format_err!("PlaySound failed: {:?}", err))?;

        Ok(())
    }

    /// Performs the actual factory reset.
    async fn reset(self: &Rc<Self>) -> Result<(), Error> {
        self.set_factory_reset_state(FactoryResetState::Resetting);
        if let Err(error) = self.play_reset_sound().await {
            tracing::info!("Failed to play reset sound: {:?}", error);
        }

        // Trigger reset
        tracing::info!("Triggering factory reset");
        let factory_reset = fuchsia_component::client::connect_to_protocol::<FactoryResetMarker>()?;
        factory_reset.reset().await?;

        Ok(())
    }
}

#[async_trait(?Send)]
impl InputHandler for FactoryResetHandler {
    /// This InputHandler doesn't consume any input events. It just passes them on to the next handler in the pipeline.
    /// Since it doesn't need exclusive access to the events this seems like the best way to avoid handlers further
    /// down the pipeline missing events that they need.
    async fn handle_input_event(
        self: Rc<Self>,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::ConsumerControls(ref event),
                device_descriptor: input_device::InputDeviceDescriptor::ConsumerControls(_),
                event_time: _,
            } => {
                match self.factory_reset_state() {
                    FactoryResetState::Idle => {
                        let event_clone = event.clone();
                        Task::local(async move { self.handle_allowed_event(&event_clone).await })
                            .detach()
                    }
                    FactoryResetState::Disallowed => self.handle_disallowed_event(event),
                    FactoryResetState::ButtonCountdown { deadline: _ } => {
                        self.handle_button_countdown_event(event)
                    }
                    FactoryResetState::ResetCountdown { deadline: _ } => {
                        self.handle_reset_countdown_event(event)
                    }
                    FactoryResetState::Resetting => {
                        tracing::warn!("Recieved an input event while factory resetting the device")
                    }
                };
            }
            _ => (),
        };

        vec![input_event]
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::consumer_controls::ConsumerControlsDeviceDescriptor,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_recovery_policy::{DeviceMarker, DeviceProxy},
        fidl_fuchsia_recovery_ui::{FactoryResetCountdownMarker, FactoryResetCountdownProxy},
        fuchsia_async::TestExecutor,
        matches::assert_matches,
        pin_utils::pin_mut,
        std::task::Poll,
    };

    fn create_countdown_handler_and_proxy() -> (Rc<FactoryResetHandler>, FactoryResetCountdownProxy)
    {
        let reset_handler = FactoryResetHandler::new();
        let (countdown_proxy, countdown_stream) =
            create_proxy_and_stream::<FactoryResetCountdownMarker>()
                .expect("Failed to create countdown proxy");

        let stream_fut =
            reset_handler.clone().handle_factory_reset_countdown_request_stream(countdown_stream);

        Task::local(async move {
            if stream_fut.await.is_err() {
                tracing::warn!("Failed to handle factory reset countdown request stream");
            }
        })
        .detach();

        (reset_handler, countdown_proxy)
    }

    fn create_recovery_policy_proxy(reset_handler: Rc<FactoryResetHandler>) -> DeviceProxy {
        let (device_proxy, device_stream) = create_proxy_and_stream::<DeviceMarker>()
            .expect("Failed to create recovery policy device proxy");

        Task::local(async move {
            if reset_handler
                .handle_recovery_policy_device_request_stream(device_stream)
                .await
                .is_err()
            {
                tracing::warn!("Failed to handle recovery policy device request stream");
            }
        })
        .detach();

        device_proxy
    }

    fn create_input_device_descriptor() -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::ConsumerControls(ConsumerControlsDeviceDescriptor {
            buttons: vec![
                fidl_input_report::ConsumerControlButton::CameraDisable,
                fidl_input_report::ConsumerControlButton::FactoryReset,
                fidl_input_report::ConsumerControlButton::MicMute,
                fidl_input_report::ConsumerControlButton::Pause,
                fidl_input_report::ConsumerControlButton::VolumeDown,
                fidl_input_report::ConsumerControlButton::VolumeUp,
            ],
        })
    }

    fn create_reset_consumer_controls_event() -> ConsumerControlsEvent {
        ConsumerControlsEvent::new(vec![fidl_input_report::ConsumerControlButton::FactoryReset])
    }

    fn create_non_reset_consumer_controls_event() -> ConsumerControlsEvent {
        ConsumerControlsEvent::new(vec![
            fidl_input_report::ConsumerControlButton::CameraDisable,
            fidl_input_report::ConsumerControlButton::MicMute,
            fidl_input_report::ConsumerControlButton::Pause,
            fidl_input_report::ConsumerControlButton::VolumeDown,
            fidl_input_report::ConsumerControlButton::VolumeUp,
        ])
    }

    fn create_non_reset_input_event() -> input_device::InputEvent {
        let device_event = input_device::InputDeviceEvent::ConsumerControls(
            create_non_reset_consumer_controls_event(),
        );

        input_device::InputEvent {
            device_event,
            device_descriptor: create_input_device_descriptor(),
            event_time: Time::now().into_nanos() as u64,
        }
    }

    fn create_reset_input_event() -> input_device::InputEvent {
        let device_event = input_device::InputDeviceEvent::ConsumerControls(
            create_reset_consumer_controls_event(),
        );

        input_device::InputEvent {
            device_event,
            device_descriptor: create_input_device_descriptor(),
            event_time: Time::now().into_nanos() as u64,
        }
    }

    fn get_countdown_state(
        proxy: &FactoryResetCountdownProxy,
        executor: &mut TestExecutor,
    ) -> FactoryResetCountdownState {
        let countdown_proxy_clone = proxy.clone();
        let countdown_state_fut = async move {
            countdown_proxy_clone.watch().await.expect("Failed to get countdown state")
        };
        pin_mut!(countdown_state_fut);

        match executor.run_until_stalled(&mut countdown_state_fut) {
            Poll::Ready(countdown_state) => countdown_state,
            _ => panic!("Failed to get countdown state"),
        }
    }

    #[fuchsia::test]
    async fn is_reset_requested_looks_for_reset_signal() {
        let reset_event = create_reset_consumer_controls_event();
        let non_reset_event = create_non_reset_consumer_controls_event();

        assert!(
            is_reset_requested(&reset_event),
            "Should reset when the reset signal is recieved."
        );
        assert!(
            !is_reset_requested(&non_reset_event),
            "Should only reset when the reset signal is recieved."
        );
    }

    #[fuchsia::test]
    async fn factory_reset_countdown_listener_gets_initial_state() {
        let (reset_handler, countdown_proxy) = create_countdown_handler_and_proxy();
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Idle);
    }

    #[test]
    fn factory_reset_countdown_listener_is_notified_on_state_change() -> Result<(), Error> {
        let mut executor = TestExecutor::new_with_fake_time().unwrap();
        let (reset_handler, countdown_proxy) = create_countdown_handler_and_proxy();

        // The initial state should be no scheduled reset time and the
        // FactoryRestHandler state should be FactoryResetState::Idle
        let countdown_state = get_countdown_state(&countdown_proxy, &mut executor);
        let handler_state = reset_handler.factory_reset_state();
        assert!(countdown_state.scheduled_reset_time.is_none());
        assert_eq!(handler_state, FactoryResetState::Idle);

        // Send a reset event
        let reset_event = create_reset_input_event();
        let handle_input_event_fut = reset_handler.clone().handle_input_event(reset_event);
        pin_mut!(handle_input_event_fut);
        assert!(executor.run_until_stalled(&mut handle_input_event_fut).is_ready());

        // The next state will be FactoryResetState::ButtonCountdown with no scheduled reset
        let countdown_state = get_countdown_state(&countdown_proxy, &mut executor);
        let handler_state = reset_handler.factory_reset_state();
        assert!(countdown_state.scheduled_reset_time.is_none());
        assert_matches!(handler_state, FactoryResetState::ButtonCountdown { deadline: _ });

        // Skip ahead 500ms for the ButtonCountdown
        executor.set_fake_time(Time::after(Duration::from_millis(500)));
        executor.wake_expired_timers();

        // After the ButtonCountdown the reset_handler enters the
        // FactoryResetState::ResetCountdown state WITH a scheduled reset time.
        let countdown_state = get_countdown_state(&countdown_proxy, &mut executor);
        let handler_state = reset_handler.factory_reset_state();
        assert!(countdown_state.scheduled_reset_time.is_some());
        assert_matches!(handler_state, FactoryResetState::ResetCountdown { deadline: _ });

        // Skip ahead 10s for the ResetCountdown
        executor.set_fake_time(Time::after(Duration::from_seconds(10)));
        executor.wake_expired_timers();

        // After the ResetCountdown the reset_handler enters the
        // FactoryResetState::Resetting state with no scheduled reset time.
        let countdown_state = get_countdown_state(&countdown_proxy, &mut executor);
        let handler_state = reset_handler.factory_reset_state();
        assert!(countdown_state.scheduled_reset_time.is_none());
        assert_eq!(handler_state, FactoryResetState::Resetting);

        Ok(())
    }

    #[fuchsia::test]
    async fn recovery_policy_requests_update_reset_handler_state() {
        let (reset_handler, countdown_proxy) = create_countdown_handler_and_proxy();

        // Initial state should be FactoryResetState::Idle with no scheduled reset
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Idle);

        // Set FactoryResetState to Disallow
        let device_proxy = create_recovery_policy_proxy(reset_handler.clone());
        device_proxy.set_is_local_reset_allowed(false).expect("Failed to set recovery policy");

        // State should now be in Disallow and scheduled_reset_time should be None
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Disallowed);

        // Send reset event
        let reset_event = create_reset_input_event();
        reset_handler.clone().handle_input_event(reset_event).await;

        // State should still be Disallow
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Disallowed);

        // Set the state back to Allow
        let device_proxy = create_recovery_policy_proxy(reset_handler.clone());
        device_proxy.set_is_local_reset_allowed(true).expect("Failed to set recovery policy");

        // State should be FactoryResetState::Idle with no scheduled reset
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Idle);
    }

    #[fuchsia::test]
    fn handle_allowed_event_changes_state_with_reset() {
        let mut executor = TestExecutor::new().unwrap();

        let reset_event = create_reset_consumer_controls_event();
        let (reset_handler, countdown_proxy) = create_countdown_handler_and_proxy();

        // Initial state should be FactoryResetState::Idle with no scheduled reset
        let reset_state = executor.run_singlethreaded(async {
            countdown_proxy.watch().await.expect("Failed to get countdown state")
        });
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Idle);

        let reset_handler_clone = reset_handler.clone();
        let handle_allowed_event_fut = reset_handler_clone.handle_allowed_event(&reset_event);
        futures::pin_mut!(handle_allowed_event_fut);
        let _ = executor.run_until_stalled(&mut handle_allowed_event_fut);

        let watch_res = executor.run_singlethreaded(countdown_proxy.watch());
        // This should result in the reset handler entering the ButtonCountdown state
        assert!(watch_res.is_ok());
        assert!(watch_res.unwrap().scheduled_reset_time.is_none());
        assert_matches!(
            reset_handler.factory_reset_state(),
            FactoryResetState::ButtonCountdown { deadline: _ }
        );
    }

    #[fuchsia::test]
    async fn handle_allowed_event_wont_change_state_without_reset() {
        let non_reset_event = create_non_reset_consumer_controls_event();
        let (reset_handler, countdown_proxy) = create_countdown_handler_and_proxy();

        // Initial state should be FactoryResetState::Idle with no scheduled reset
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Idle);

        reset_handler.clone().handle_allowed_event(&non_reset_event).await;

        // This should result in the reset handler staying in the Allowed state
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Idle);
    }

    #[fuchsia::test]
    async fn handle_disallowed_event_wont_change_state() {
        let reset_event = create_reset_consumer_controls_event();
        let non_reset_event = create_non_reset_consumer_controls_event();
        let reset_handler = FactoryResetHandler::new();

        *reset_handler.factory_reset_state.borrow_mut() = FactoryResetState::Disallowed;

        // Calling handle_disallowed_event shouldn't change the state no matter
        // what the contents of the event are
        reset_handler.handle_disallowed_event(&reset_event);
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Disallowed);

        reset_handler.handle_disallowed_event(&non_reset_event);
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Disallowed);
    }

    #[fuchsia::test]
    async fn handle_button_countdown_event_changes_state_when_reset_no_longer_requested() {
        let non_reset_event = create_non_reset_consumer_controls_event();
        let reset_handler = FactoryResetHandler::new();

        let deadline = Time::after(BUTTON_TIMEOUT);
        *reset_handler.factory_reset_state.borrow_mut() =
            FactoryResetState::ButtonCountdown { deadline };

        // Calling handle_button_countdown_event should reset the handler
        // to the idle state
        reset_handler.handle_button_countdown_event(&non_reset_event);
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Idle);
    }

    #[fuchsia::test]
    async fn handle_reset_countdown_event_changes_state_when_reset_no_longer_requested() {
        let non_reset_event = create_non_reset_consumer_controls_event();
        let reset_handler = FactoryResetHandler::new();

        *reset_handler.factory_reset_state.borrow_mut() =
            FactoryResetState::ResetCountdown { deadline: Time::now() };

        // Calling handle_reset_countdown_event should reset the handler
        // to the idle state
        reset_handler.handle_reset_countdown_event(&non_reset_event);
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Idle);
    }

    #[fuchsia::test]
    async fn factory_reset_disallowed_during_button_countdown() {
        let (reset_handler, countdown_proxy) = create_countdown_handler_and_proxy();

        // Initial state should be FactoryResetState::Idle with no scheduled reset
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Idle);

        // Send reset event
        let reset_event = create_reset_input_event();
        reset_handler.clone().handle_input_event(reset_event).await;

        // State should now be ButtonCountdown and scheduled_reset_time should be None
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_matches!(
            reset_handler.factory_reset_state(),
            FactoryResetState::ButtonCountdown { deadline: _ }
        );

        // Set FactoryResetState to Disallow
        let device_proxy = create_recovery_policy_proxy(reset_handler.clone());
        device_proxy.set_is_local_reset_allowed(false).expect("Failed to set recovery policy");

        // State should now be in Disallow and scheduled_reset_time should be None
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Disallowed);
    }

    #[fuchsia::test]
    async fn factory_reset_disallowed_during_reset_countdown() {
        let (reset_handler, countdown_proxy) = create_countdown_handler_and_proxy();

        // Initial state should be FactoryResetState::Idle with no scheduled reset
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Idle);

        // Send reset event
        let reset_event = create_reset_input_event();
        reset_handler.clone().handle_input_event(reset_event).await;

        // State should now be ButtonCountdown and scheduled_reset_time should be None
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_matches!(
            reset_handler.factory_reset_state(),
            FactoryResetState::ButtonCountdown { deadline: _ }
        );

        // State should now be ResetCountdown and scheduled_reset_time should be Some
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_some());
        assert_matches!(
            reset_handler.factory_reset_state(),
            FactoryResetState::ResetCountdown { deadline: _ }
        );

        // Set FactoryResetState to Disallow
        let device_proxy = create_recovery_policy_proxy(reset_handler.clone());
        device_proxy.set_is_local_reset_allowed(false).expect("Failed to set recovery policy");

        // State should now be in Disallow and scheduled_reset_time should be None
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Disallowed);
    }

    #[fuchsia::test]
    async fn factory_reset_cancelled_during_button_countdown() {
        let (reset_handler, countdown_proxy) = create_countdown_handler_and_proxy();

        // Initial state should be FactoryResetState::Idle with no scheduled reset
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Idle);

        // Send reset event
        let reset_event = create_reset_input_event();
        reset_handler.clone().handle_input_event(reset_event).await;

        // State should now be ButtonCountdown and scheduled_reset_time should be None
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_matches!(
            reset_handler.factory_reset_state(),
            FactoryResetState::ButtonCountdown { deadline: _ }
        );

        // Pass in an event to simulate releasing the reset button
        let non_reset_event = create_non_reset_input_event();
        reset_handler.clone().handle_input_event(non_reset_event).await;

        // State should now be in Idle and scheduled_reset_time should be None
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Idle);
    }

    #[fuchsia::test]
    async fn factory_reset_cancelled_during_reset_countdown() {
        let (reset_handler, countdown_proxy) = create_countdown_handler_and_proxy();

        // Initial state should be FactoryResetState::Idle with no scheduled reset
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Idle);

        // Send reset event
        let reset_event = create_reset_input_event();
        reset_handler.clone().handle_input_event(reset_event).await;

        // State should now be ButtonCountdown and scheduled_reset_time should be None
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_matches!(
            reset_handler.factory_reset_state(),
            FactoryResetState::ButtonCountdown { deadline: _ }
        );

        // State should now be ResetCountdown and scheduled_reset_time should be Some
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_some());
        assert_matches!(
            reset_handler.factory_reset_state(),
            FactoryResetState::ResetCountdown { deadline: _ }
        );

        // Pass in an event to simulate releasing the reset button
        let non_reset_event = create_non_reset_input_event();
        reset_handler.clone().handle_input_event(non_reset_event).await;

        // State should now be in Idle and scheduled_reset_time should be None
        let reset_state = countdown_proxy.watch().await.expect("Failed to get countdown state");
        assert!(reset_state.scheduled_reset_time.is_none());
        assert_eq!(reset_handler.factory_reset_state(), FactoryResetState::Idle);
    }
}
