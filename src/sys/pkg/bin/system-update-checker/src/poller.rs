// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::apply::Initiator;
use crate::config::Config;
use crate::update_manager::{
    CurrentChannelUpdater, TargetChannelUpdater, UpdateApplier, UpdateChecker, UpdateManager,
};
use crate::update_monitor::StateChangeCallback;
use fidl_fuchsia_update::CheckStartedResult;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_info;
use futures::prelude::*;
use std::sync::Arc;

pub fn run_periodic_update_check<T, Ch, C, A, S>(
    manager: Arc<UpdateManager<T, Ch, C, A, S>>,
    config: &Config,
) -> impl Future<Output = ()>
where
    T: TargetChannelUpdater,
    Ch: CurrentChannelUpdater,
    C: UpdateChecker,
    A: UpdateApplier,
    S: StateChangeCallback,
{
    let timer = config.poll_frequency().map(|duration| fasync::Interval::new(duration.into()));

    async move {
        let mut timer = match timer {
            Some(timer) => timer,
            None => return,
        };

        while let Some(()) = timer.next().await {
            match manager.try_start_update(Initiator::Automatic, None) {
                CheckStartedResult::Started => {}
                CheckStartedResult::Throttled => {
                    fx_log_info!("Automatic update check throttled");
                }
                CheckStartedResult::InProgress => {
                    fx_log_info!("Update in progress, automatic update check skipped");
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::ConfigBuilder;
    use crate::update_manager::tests::{
        FakeCurrentChannelUpdater, FakeLastUpdateStorage, FakeTargetChannelUpdater,
        FakeUpdateChecker, StateChangeCollector, UnreachableStateChangeCallback,
        UnreachableUpdateApplier,
    };
    use fidl_fuchsia_update::ManagerState;
    use fuchsia_async::DurationExt;
    use fuchsia_zircon::DurationNum;
    use futures::task::Poll;

    #[test]
    fn test_disabled_periodic_update_check() {
        let mut executor = fasync::Executor::new_with_fake_time().unwrap();

        let checker = FakeUpdateChecker::new_up_to_date();
        let manager: UpdateManager<_, _, _, _, UnreachableStateChangeCallback> =
            UpdateManager::from_checker_and_applier(
                FakeTargetChannelUpdater::new(),
                FakeCurrentChannelUpdater::new(),
                checker.clone(),
                UnreachableUpdateApplier,
                FakeLastUpdateStorage::new(),
            );

        let mut cron = run_periodic_update_check(Arc::new(manager), &Config::default()).boxed();

        assert_eq!(Poll::Ready(()), executor.run_until_stalled(&mut cron));
        assert_eq!(checker.call_count(), 0);
        assert_eq!(None, executor.wake_next_timer());
    }

    #[test]
    fn test_periodic_update_check() {
        let mut executor = fasync::Executor::new_with_fake_time().unwrap();

        let checker = FakeUpdateChecker::new_up_to_date();
        let callback = StateChangeCollector::new();
        let manager = UpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeCurrentChannelUpdater::new(),
            checker.clone(),
            UnreachableUpdateApplier,
            FakeLastUpdateStorage::new(),
        );
        manager.add_permanent_callback(callback.clone());

        let period = 10.minutes();
        let config = ConfigBuilder::new().poll_frequency(period).build();
        let mut cron = run_periodic_update_check(Arc::new(manager), &config).boxed();

        // Let the cron task set up the timer, but nothing interesting happens until time advances.
        assert_eq!(Poll::Pending, executor.run_until_stalled(&mut cron));
        assert_eq!(callback.take_states(), vec![ManagerState::Idle.into()]);

        // Not time yet.
        executor.set_fake_time(8.minutes().after_now());
        assert!(!executor.wake_expired_timers());
        assert_eq!(Poll::Pending, executor.run_until_stalled(&mut cron));
        assert_eq!(callback.take_states(), vec![]);

        // Verify the timer performs the correct operations after elapsing.
        executor.set_fake_time(2.minutes().after_now());
        assert!(executor.wake_expired_timers());
        assert_eq!(Poll::Pending, executor.run_until_stalled(&mut cron));
        assert_eq!(checker.call_count(), 1);
        assert_eq!(
            callback.take_states(),
            vec![ManagerState::CheckingForUpdates.into(), ManagerState::Idle.into()]
        );

        // Verify the timer fires more than once.
        executor.set_fake_time(period.after_now());
        assert!(executor.wake_expired_timers());
        assert_eq!(Poll::Pending, executor.run_until_stalled(&mut cron));
        assert_eq!(checker.call_count(), 2);
        assert_eq!(
            callback.take_states(),
            vec![ManagerState::CheckingForUpdates.into(), ManagerState::Idle.into()]
        );
    }

    #[test]
    fn test_simultaneous_user_update_check_and_periodic_update_check() {
        let mut executor = fasync::Executor::new_with_fake_time().unwrap();

        let checker = FakeUpdateChecker::new_up_to_date();
        let update_blocked = checker.block().unwrap();
        let callback = StateChangeCollector::new();
        let manager = UpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeCurrentChannelUpdater::new(),
            checker.clone(),
            UnreachableUpdateApplier,
            FakeLastUpdateStorage::new(),
        );
        let manager = Arc::new(manager);
        manager.add_permanent_callback(callback.clone());

        let period = 24.hours();
        let config = ConfigBuilder::new().poll_frequency(period).build();
        let mut cron = run_periodic_update_check(Arc::clone(&manager), &config).boxed();

        // Let the cron task set up the timer, but nothing interesting happens until time advances.
        assert_eq!(Poll::Pending, executor.run_until_stalled(&mut cron));

        // User wins, and only 1 update check happens.
        assert_eq!(manager.try_start_update(Initiator::Manual, None), CheckStartedResult::Started);
        // The automatic update is skipped because an update is already in progress.
        executor.set_fake_time(period.after_now());
        assert!(executor.wake_expired_timers());
        assert_eq!(Poll::Pending, executor.run_until_stalled(&mut cron));
        // Let the user-initiated update check complete. The update does not run.
        std::mem::drop(update_blocked);
        assert_eq!(Poll::Pending, executor.run_until_stalled(&mut cron));

        assert_eq!(checker.call_count(), 1);
        assert_eq!(
            callback.take_states(),
            vec![
                ManagerState::Idle.into(),
                ManagerState::CheckingForUpdates.into(),
                ManagerState::Idle.into()
            ]
        );
    }
}
