// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    async_utils::hanging_get::server::HangingGet,
    fidl_fuchsia_input_interaction::{
        NotifierRequest, NotifierRequestStream, NotifierWatchStateResponder, State,
    },
    fidl_fuchsia_input_interaction_observation::{AggregatorRequest, AggregatorRequestStream},
    fuchsia_async::{Task, Timer},
    fuchsia_syslog::fx_log_info,
    fuchsia_zircon as zx,
    futures::StreamExt,
    std::{
        cell::{Cell, RefCell},
        rc::Rc,
    },
};

const ACTIVITY_RATE_LIMIT: zx::Duration = zx::Duration::from_minutes(1);

type NotifyFn = Box<dyn Fn(&State, NotifierWatchStateResponder) -> bool>;
type InteractionHangingGet = HangingGet<State, NotifierWatchStateResponder, NotifyFn>;

/// An [`ActivityManager`] tracks the state of user input interaction activity.
pub struct ActivityManager {
    interaction_hanging_get: RefCell<InteractionHangingGet>,
    idle_threshold_minutes: zx::Duration,
    idle_transition_task: Cell<Option<Task<()>>>,
    last_event_time: RefCell<zx::Time>,
}

impl ActivityManager {
    /// Creates a new [`ActivityManager`] that listens for user input
    /// input interactions and notifies clients of activity state changes.
    pub fn new(idle_threshold_minutes: zx::Duration) -> Rc<Self> {
        Self::new_internal(idle_threshold_minutes, zx::Time::get_monotonic())
    }

    #[cfg(test)]
    /// Sets the initial idleness timer relative to fake time at 0 for tests.
    fn new_for_test(idle_threshold_minutes: zx::Duration) -> Rc<Self> {
        Self::new_internal(idle_threshold_minutes, zx::Time::ZERO)
    }

    fn new_internal(idle_threshold_minutes: zx::Duration, initial_timestamp: zx::Time) -> Rc<Self> {
        let initial_state = State::Active;

        let interaction_hanging_get = ActivityManager::init_hanging_get(initial_state);
        let state_publisher = interaction_hanging_get.new_publisher();
        let idle_transition_task = Task::local(async move {
            Timer::new(initial_timestamp + idle_threshold_minutes).await;
            state_publisher.set(State::Idle);
        });

        Rc::new(Self {
            interaction_hanging_get: RefCell::new(interaction_hanging_get),
            idle_threshold_minutes,
            idle_transition_task: Cell::new(Some(idle_transition_task)),
            last_event_time: RefCell::new(initial_timestamp),
        })
    }

    /// Handles the request stream for
    /// fuchsia.input.interaction.observation.Aggregator.
    ///
    /// # Parameters
    /// `stream`: The `AggregatorRequestStream` to be handled.
    pub async fn handle_interaction_aggregator_request_stream(
        self: Rc<Self>,
        mut stream: AggregatorRequestStream,
    ) -> Result<(), Error> {
        while let Some(aggregator_request) = stream.next().await {
            let AggregatorRequest::ReportDiscreteActivity { event_time, responder } =
                aggregator_request?;

            // Clamp the time to now so that clients cannot send events far off
            // in the future to keep the system always active.
            // Note: We use the global executor to get the current time instead
            // of the kernel so that we do not unnecessarily clamp
            // test-injected times.
            let event_time = zx::Time::from_nanos(event_time)
                .clamp(zx::Time::ZERO, fuchsia_async::Time::now().into_zx());

            if *self.last_event_time.borrow() > event_time {
                let _: Result<(), fidl::Error> = responder.send();
                continue;
            }

            let state_publisher = self.interaction_hanging_get.borrow().new_publisher();
            if let Some(t) = self.idle_transition_task.take() {
                // If the task returns a completed output, we can assume the
                // state has transitioned to Idle.
                if let Some(()) = t.cancel().await {
                    state_publisher.set(State::Active);
                }
            }

            let _: Result<(), fidl::Error> = responder.send();

            // Rate-limit subsequent events within the threshold.
            *self.last_event_time.borrow_mut() = event_time + ACTIVITY_RATE_LIMIT;
            let idle_threshold_minutes = self.idle_threshold_minutes;
            self.idle_transition_task.set(Some(Task::local(async move {
                Timer::new(event_time + idle_threshold_minutes).await;
                state_publisher.set(State::Idle);
            })));
        }

        Ok(())
    }

    /// Handles the request stream for fuchsia.input.interaction.Notifier.
    ///
    /// # Parameters
    /// `stream`: The `NotifierRequestStream` to be handled.
    pub async fn handle_interaction_notifier_request_stream(
        self: Rc<Self>,
        mut stream: NotifierRequestStream,
    ) -> Result<(), Error> {
        let subscriber = self.interaction_hanging_get.borrow_mut().new_subscriber();

        while let Some(notifier_request) = stream.next().await {
            let NotifierRequest::WatchState { responder } = notifier_request?;
            subscriber.register(responder)?;
        }

        Ok(())
    }

    fn init_hanging_get(initial_state: State) -> InteractionHangingGet {
        let notify_fn: NotifyFn = Box::new(|state, responder| {
            if responder.send(*state).is_err() {
                fx_log_info!("Failed to send user input interaction state");
            }

            true
        });

        InteractionHangingGet::new(initial_state, notify_fn)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        async_utils::hanging_get::client::HangingGetStream,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_input_interaction::{NotifierMarker, NotifierProxy},
        fidl_fuchsia_input_interaction_observation::{AggregatorMarker, AggregatorProxy},
        fuchsia_async::TestExecutor,
        pin_utils::pin_mut,
        std::task::Poll,
    };

    const ACTIVITY_TIMEOUT: zx::Duration = zx::Duration::from_minutes(2);

    fn create_interaction_aggregator_proxy(
        activity_manager: Rc<ActivityManager>,
    ) -> AggregatorProxy {
        let (aggregator_proxy, aggregator_stream) = create_proxy_and_stream::<AggregatorMarker>()
            .expect("Failed to create aggregator proxy");

        Task::local(async move {
            if activity_manager
                .handle_interaction_aggregator_request_stream(aggregator_stream)
                .await
                .is_err()
            {
                panic!("Failed to handle aggregator request stream");
            }
        })
        .detach();

        aggregator_proxy
    }

    fn create_interaction_notifier_proxy(activity_manager: Rc<ActivityManager>) -> NotifierProxy {
        let (notifier_proxy, notifier_stream) =
            create_proxy_and_stream::<NotifierMarker>().expect("Failed to create notifier proxy");

        let stream_fut =
            activity_manager.clone().handle_interaction_notifier_request_stream(notifier_stream);

        Task::local(async move {
            if stream_fut.await.is_err() {
                panic!("Failed to handle notifier request stream");
            }
        })
        .detach();

        notifier_proxy
    }

    #[fuchsia::test]
    async fn interaction_aggregator_reports_activity() {
        let activity_manager = ActivityManager::new_for_test(ACTIVITY_TIMEOUT);
        let proxy = create_interaction_aggregator_proxy(activity_manager.clone());
        proxy.report_discrete_activity(0).await.expect("Failed to report activity");
    }

    #[fuchsia::test]
    async fn interaction_notifier_listener_gets_initial_state() {
        let activity_manager = ActivityManager::new_for_test(ACTIVITY_TIMEOUT);
        let notifier_proxy = create_interaction_notifier_proxy(activity_manager.clone());
        let state = notifier_proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, State::Active);
    }

    #[fuchsia::test]
    fn interaction_notifier_listener_gets_updated_idle_state() -> Result<(), Error> {
        let mut executor = TestExecutor::new_with_fake_time().unwrap();
        executor.set_fake_time(fuchsia_async::Time::from_nanos(0));

        let activity_manager = ActivityManager::new_for_test(ACTIVITY_TIMEOUT);
        let notifier_proxy = create_interaction_notifier_proxy(activity_manager.clone());

        // Initial state is active.
        let mut watch_state_stream =
            HangingGetStream::new(notifier_proxy, NotifierProxy::watch_state);
        let state_fut = watch_state_stream.next();
        pin_mut!(state_fut);
        let initial_state = executor.run_until_stalled(&mut state_fut);
        assert_matches!(initial_state, Poll::Ready(Some(Ok(State::Active))));

        // Skip ahead by the activity timeout.
        executor.set_fake_time(fuchsia_async::Time::after(ACTIVITY_TIMEOUT));
        assert_eq!(executor.wake_expired_timers(), true);

        // State transitions to Idle.
        let idle_state_fut = watch_state_stream.next();
        pin_mut!(idle_state_fut);
        let initial_state = executor.run_until_stalled(&mut idle_state_fut);
        assert_matches!(initial_state, Poll::Ready(Some(Ok(State::Idle))));

        Ok(())
    }

    #[fuchsia::test]
    fn interaction_notifier_listener_gets_updated_active_state() -> Result<(), Error> {
        let mut executor = TestExecutor::new_with_fake_time().unwrap();
        executor.set_fake_time(fuchsia_async::Time::from_nanos(0));

        let activity_manager = ActivityManager::new_for_test(ACTIVITY_TIMEOUT);
        let notifier_proxy = create_interaction_notifier_proxy(activity_manager.clone());

        // Initial state is active.
        let mut watch_state_stream =
            HangingGetStream::new(notifier_proxy, NotifierProxy::watch_state);
        let state_fut = watch_state_stream.next();
        pin_mut!(state_fut);
        let initial_state = executor.run_until_stalled(&mut state_fut);
        assert_matches!(initial_state, Poll::Ready(Some(Ok(State::Active))));

        // Skip ahead by the activity timeout.
        executor.set_fake_time(fuchsia_async::Time::after(ACTIVITY_TIMEOUT));
        assert_eq!(executor.wake_expired_timers(), true);

        // State transitions to Idle.
        let idle_state_fut = watch_state_stream.next();
        pin_mut!(idle_state_fut);
        let initial_state = executor.run_until_stalled(&mut idle_state_fut);
        assert_matches!(initial_state, Poll::Ready(Some(Ok(State::Idle))));

        // Send an activity.
        let proxy = create_interaction_aggregator_proxy(activity_manager.clone());
        let report_fut = proxy.report_discrete_activity(ACTIVITY_TIMEOUT.into_nanos());
        pin_mut!(report_fut);
        assert!(executor.run_until_stalled(&mut report_fut).is_ready());

        // State transitions to Active.
        let active_state_fut = watch_state_stream.next();
        pin_mut!(active_state_fut);
        let initial_state = executor.run_until_stalled(&mut active_state_fut);
        assert_matches!(initial_state, Poll::Ready(Some(Ok(State::Active))));

        Ok(())
    }

    #[fuchsia::test]
    fn activity_manager_drops_first_timer_on_activity() -> Result<(), Error> {
        // This test does the following:
        //   - Start an activity manager, whose initial timeout is set to
        //     ACTIVITY_TIMEOUT.
        //   - Send an activity at time ACTIVITY_TIMEOUT / 2.
        //   - Observe that after ACTIVITY_TIMEOUT transpires, the initial
        //     timeout to transition to idle state _does not_ fire, as we
        //     expect it to be replaced by a new timeout in response to the
        //     injected activity.
        //   - Observe that after ACTIVITY_TIMEOUT * 1.5 transpires, the second
        //     timeout to transition to idle state _does_ fire.
        // Because division will round to 0, odd-number timeouts could cause an
        // incorrect implementation to still pass the test. In order to catch
        // these cases, we first assert that ACTIVITY_TIMEOUT is an even number.
        assert_eq!(ACTIVITY_TIMEOUT.into_nanos() % 2, 0);

        let mut executor = TestExecutor::new_with_fake_time().unwrap();
        executor.set_fake_time(fuchsia_async::Time::from_nanos(0));

        let activity_manager = ActivityManager::new_for_test(ACTIVITY_TIMEOUT);
        let notifier_proxy = create_interaction_notifier_proxy(activity_manager.clone());

        // Initial state is active.
        let mut watch_state_stream =
            HangingGetStream::new(notifier_proxy, NotifierProxy::watch_state);
        let state_fut = watch_state_stream.next();
        pin_mut!(state_fut);
        let initial_state = executor.run_until_stalled(&mut state_fut);
        assert_matches!(initial_state, Poll::Ready(Some(Ok(State::Active))));

        // Skip ahead by half the activity timeout. No timer fires.
        executor.set_fake_time(fuchsia_async::Time::after(ACTIVITY_TIMEOUT / 2));
        assert_eq!(executor.wake_expired_timers(), false);

        // Send an activity, replacing the initial idleness timer.
        let proxy = create_interaction_aggregator_proxy(activity_manager.clone());
        let report_fut = proxy.report_discrete_activity((ACTIVITY_TIMEOUT / 2).into_nanos());
        pin_mut!(report_fut);
        assert!(executor.run_until_stalled(&mut report_fut).is_ready());

        // Skip ahead by half the activity timeout.
        executor.set_fake_time(fuchsia_async::Time::after(ACTIVITY_TIMEOUT / 2));
        assert_eq!(executor.wake_expired_timers(), false);

        // Initial timer does not fire.
        let watch_state_fut = watch_state_stream.next();
        pin_mut!(watch_state_fut);
        let watch_state_res = executor.run_until_stalled(&mut watch_state_fut);
        assert_matches!(watch_state_res, Poll::Pending);

        // Skip ahead by half the activity timeout.
        executor.set_fake_time(fuchsia_async::Time::after(ACTIVITY_TIMEOUT / 2));
        assert_eq!(executor.wake_expired_timers(), true);

        // Second timer does fire.
        let watch_state_res = executor.run_until_stalled(&mut watch_state_fut);
        assert_matches!(watch_state_res, Poll::Ready(Some(Ok(State::Idle))));

        Ok(())
    }

    #[fuchsia::test]
    fn actvity_manager_drops_late_activities() -> Result<(), Error> {
        let mut executor = TestExecutor::new_with_fake_time().unwrap();
        executor.set_fake_time(fuchsia_async::Time::from_nanos(0));

        let activity_manager = ActivityManager::new_for_test(ACTIVITY_TIMEOUT);
        let notifier_proxy = create_interaction_notifier_proxy(activity_manager.clone());

        // Initial state is active.
        let mut watch_state_stream =
            HangingGetStream::new(notifier_proxy, NotifierProxy::watch_state);
        let state_fut = watch_state_stream.next();
        pin_mut!(state_fut);
        let watch_state_res = executor.run_until_stalled(&mut state_fut);
        assert_matches!(watch_state_res, Poll::Ready(Some(Ok(State::Active))));

        // Skip ahead by half the activity timeout. No timer fires.
        executor.set_fake_time(fuchsia_async::Time::after(ACTIVITY_TIMEOUT / 2));
        assert_eq!(executor.wake_expired_timers(), false);

        // Send an activity, replacing the initial idleness timer.
        let proxy = create_interaction_aggregator_proxy(activity_manager.clone());
        let report_fut = proxy.report_discrete_activity((ACTIVITY_TIMEOUT / 2).into_nanos());
        pin_mut!(report_fut);
        assert!(executor.run_until_stalled(&mut report_fut).is_ready());

        // Skip ahead by half the activity timeout.
        executor.set_fake_time(fuchsia_async::Time::after(ACTIVITY_TIMEOUT / 2));
        assert_eq!(executor.wake_expired_timers(), false);

        // Send an activity with an earlier time.
        let proxy = create_interaction_aggregator_proxy(activity_manager.clone());
        let report_fut = proxy.report_discrete_activity(0);
        pin_mut!(report_fut);
        assert!(executor.run_until_stalled(&mut report_fut).is_ready());

        // Initial timer does not fire, nor does one from the "earlier"
        // activity that was received later.
        let watch_state_fut = watch_state_stream.next();
        pin_mut!(watch_state_fut);
        let initial_state = executor.run_until_stalled(&mut watch_state_fut);
        assert_matches!(initial_state, Poll::Pending);

        // Skip ahead by half the activity timeout.
        executor.set_fake_time(fuchsia_async::Time::after(ACTIVITY_TIMEOUT / 2));
        assert_eq!(executor.wake_expired_timers(), true);

        // Second timer does fire.
        let watch_state_res = executor.run_until_stalled(&mut watch_state_fut);
        assert_matches!(watch_state_res, Poll::Ready(Some(Ok(State::Idle))));

        Ok(())
    }

    #[fuchsia::test]
    fn activity_manager_rate_limits_activities_notifies_idle_state() -> Result<(), Error> {
        let less_than_one_minute = zx::Duration::from_seconds(59);
        let mut executor = TestExecutor::new_with_fake_time().unwrap();
        executor.set_fake_time(fuchsia_async::Time::from_nanos(0));

        let activity_manager = ActivityManager::new_for_test(ACTIVITY_TIMEOUT);
        let notifier_proxy = create_interaction_notifier_proxy(activity_manager.clone());

        // Initial state is active.
        let mut watch_state_stream =
            HangingGetStream::new(notifier_proxy, NotifierProxy::watch_state);
        let state_fut = watch_state_stream.next();
        pin_mut!(state_fut);
        let watch_state_res = executor.run_until_stalled(&mut state_fut);
        assert_matches!(watch_state_res, Poll::Ready(Some(Ok(State::Active))));

        // Send an activity at time 0 to start rate-limiting.
        let proxy = create_interaction_aggregator_proxy(activity_manager.clone());
        let report_fut = proxy.report_discrete_activity(0);
        pin_mut!(report_fut);
        assert!(executor.run_until_stalled(&mut report_fut).is_ready());

        // Skip ahead by less than one minute, since we rate-limit events
        // within the same minute.
        executor.set_fake_time(fuchsia_async::Time::after(less_than_one_minute));
        assert_eq!(executor.wake_expired_timers(), false);

        // Send an activity. This should not replace the initial timer.
        let report_fut = proxy.report_discrete_activity(less_than_one_minute.into_nanos());
        pin_mut!(report_fut);
        assert!(executor.run_until_stalled(&mut report_fut).is_ready());

        // Skip ahead by the remainder of the initial activity timeout.
        // Initial timer should fire, as the later activity within the same
        // minute should have been dropped.
        executor.set_fake_time(fuchsia_async::Time::after(ACTIVITY_TIMEOUT - less_than_one_minute));
        assert_eq!(executor.wake_expired_timers(), true);

        let watch_state_fut = watch_state_stream.next();
        pin_mut!(watch_state_fut);
        let watch_state_res = executor.run_until_stalled(&mut watch_state_fut);
        assert_matches!(watch_state_res, Poll::Ready(Some(Ok(State::Idle))));

        // Skip ahead by less than one minute to the would-be timeout of the
        // later event, to make sure a second timer was never created.
        executor.set_fake_time(fuchsia_async::Time::after(less_than_one_minute));
        assert_eq!(executor.wake_expired_timers(), false);

        Ok(())
    }

    #[fuchsia::test]
    fn activity_manager_rate_limits_activities_notifies_active_state() -> Result<(), Error> {
        let less_than_one_minute = zx::Duration::from_seconds(59);
        let mut executor = TestExecutor::new_with_fake_time().unwrap();
        executor.set_fake_time(fuchsia_async::Time::from_nanos(0));

        let activity_manager = ActivityManager::new_for_test(ACTIVITY_TIMEOUT);
        let notifier_proxy = create_interaction_notifier_proxy(activity_manager.clone());

        // Initial state is active.
        let mut watch_state_stream =
            HangingGetStream::new(notifier_proxy, NotifierProxy::watch_state);
        let state_fut = watch_state_stream.next();
        pin_mut!(state_fut);
        let watch_state_res = executor.run_until_stalled(&mut state_fut);
        assert_matches!(watch_state_res, Poll::Ready(Some(Ok(State::Active))));

        // Send an activity at time 0 to start rate-limiting.
        let proxy = create_interaction_aggregator_proxy(activity_manager.clone());
        let report_fut = proxy.report_discrete_activity(0);
        pin_mut!(report_fut);
        assert!(executor.run_until_stalled(&mut report_fut).is_ready());

        // Skip ahead by less than one minute, since we rate-limit events
        // within the same minute.
        executor.set_fake_time(fuchsia_async::Time::after(less_than_one_minute));
        assert_eq!(executor.wake_expired_timers(), false);

        // Send an activity. This should not replace the initial timer.
        let report_fut = proxy.report_discrete_activity(less_than_one_minute.into_nanos());
        pin_mut!(report_fut);
        assert!(executor.run_until_stalled(&mut report_fut).is_ready());

        // Skip ahead by the remainder of the initial activity timeout.
        // Initial timer should fire, as the later activity within the same
        // minute should have been dropped.
        executor.set_fake_time(fuchsia_async::Time::after(ACTIVITY_TIMEOUT - less_than_one_minute));
        assert_eq!(executor.wake_expired_timers(), true);

        // State transitions to Idle.
        let watch_state_fut = watch_state_stream.next();
        pin_mut!(watch_state_fut);
        let watch_state_res = executor.run_until_stalled(&mut watch_state_fut);
        assert_matches!(watch_state_res, Poll::Ready(Some(Ok(State::Idle))));

        // Send an activity at the current time.
        let proxy = create_interaction_aggregator_proxy(activity_manager.clone());
        let report_fut = proxy.report_discrete_activity(ACTIVITY_TIMEOUT.into_nanos());
        pin_mut!(report_fut);
        assert!(executor.run_until_stalled(&mut report_fut).is_ready());

        // State transitions to Active.
        let watch_state_fut = watch_state_stream.next();
        pin_mut!(watch_state_fut);
        let watch_state_res = executor.run_until_stalled(&mut watch_state_fut);
        assert_matches!(watch_state_res, Poll::Ready(Some(Ok(State::Active))));

        Ok(())
    }
}
