// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::config::Config;
use crate::update_manager::UpdateManagerControlHandle;
use crate::update_monitor::{AttemptNotifier, StateNotifier};
use fidl_fuchsia_update::CheckNotStartedReason;
use fidl_fuchsia_update_ext::{CheckOptions, Initiator};
use fuchsia_async as fasync;
use futures::prelude::*;
use tracing::info;

pub fn run_periodic_update_check<N, A>(
    mut manager: UpdateManagerControlHandle<N, A>,
    config: &Config,
) -> impl Future<Output = ()>
where
    N: StateNotifier,
    A: AttemptNotifier,
{
    let timer = config.poll_frequency().map(fasync::Interval::new);

    async move {
        let mut timer = match timer {
            Some(timer) => timer,
            None => return,
        };

        while let Some(()) = timer.next().await {
            let options = CheckOptions::builder().initiator(Initiator::Service).build();
            match manager.try_start_update(options, None).await {
                Ok(()) => {}
                Err(CheckNotStartedReason::Throttled) => {
                    info!("Service initiated update check throttled");
                }
                Err(CheckNotStartedReason::AlreadyInProgress) => {
                    info!("Update in progress, automatic update check skipped");
                }
                Err(CheckNotStartedReason::Internal) => {
                    info!("Internal error, will try again later");
                }
                Err(CheckNotStartedReason::InvalidOptions) => {
                    info!("Invalid options, update check skipped");
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::ConfigBuilder;
    use crate::update_manager::{
        tests::{
            FakeAttemptNotifier, FakeCommitQuerier, FakeTargetChannelUpdater, FakeUpdateChecker,
            FakeUpdateManagerControlHandle, StateChangeCollector, UnreachableUpdateApplier,
        },
        UpdateManager, UpdateManagerRequest,
    };
    use assert_matches::assert_matches;
    use fidl_fuchsia_update_ext::{CheckOptions, State};
    use fuchsia_async::DurationExt;
    use fuchsia_zircon::DurationNum;
    use futures::task::Poll;
    use std::sync::Arc;

    #[test]
    fn test_disabled_periodic_update_check() {
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();

        let (manager, mut requests) =
            FakeUpdateManagerControlHandle::<StateChangeCollector, FakeAttemptNotifier>::new();

        let mut cron = run_periodic_update_check(manager, &Config::default()).boxed();

        assert_eq!(Poll::Ready(()), executor.run_until_stalled(&mut cron));
        assert_matches!(requests.next(), None);
        assert_eq!(None, executor.wake_next_timer());
    }

    #[test]
    fn test_periodic_update_check() {
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();

        let (manager, mut requests) =
            FakeUpdateManagerControlHandle::<StateChangeCollector, FakeAttemptNotifier>::new();

        let period = 10.minutes();
        let config = ConfigBuilder::new().poll_frequency(period).build();
        let mut cron = run_periodic_update_check(manager, &config).boxed();

        // Let the cron task set up the timer, but nothing interesting happens until time advances.
        assert_eq!(Poll::Pending, executor.run_until_stalled(&mut cron));
        assert_matches!(requests.next(), None);

        // Not time yet.
        executor.set_fake_time(8.minutes().after_now());
        assert!(!executor.wake_expired_timers());
        assert_eq!(Poll::Pending, executor.run_until_stalled(&mut cron));
        assert_matches!(requests.next(), None);

        // Verify the timer performs the correct operations after elapsing.
        executor.set_fake_time(2.minutes().after_now());
        assert!(executor.wake_expired_timers());
        assert_eq!(Poll::Pending, executor.run_until_stalled(&mut cron));
        assert_matches!(
            requests.next(),
            Some(UpdateManagerRequest::TryStartUpdate {
                options: CheckOptions {
                    initiator: Initiator::Service,
                    allow_attaching_to_existing_update_check: false,
                },
                callback: None,
                responder: _,
            })
        );
        assert_eq!(Poll::Pending, executor.run_until_stalled(&mut cron));
        assert_matches!(requests.next(), None);

        // Verify the timer fires more than once.
        executor.set_fake_time(period.after_now());
        assert!(executor.wake_expired_timers());
        assert_eq!(Poll::Pending, executor.run_until_stalled(&mut cron));
        assert_matches!(
            requests.next(),
            Some(UpdateManagerRequest::TryStartUpdate {
                options: CheckOptions {
                    initiator: Initiator::Service,
                    allow_attaching_to_existing_update_check: false,
                },
                callback: None,
                responder: _,
            })
        );
        assert_eq!(Poll::Pending, executor.run_until_stalled(&mut cron));
        assert_matches!(requests.next(), None);
    }

    #[test]
    fn test_simultaneous_user_update_check_and_periodic_update_check() {
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();

        let checker = FakeUpdateChecker::new_up_to_date();
        let update_blocked = checker.block().unwrap();
        let callback = StateChangeCollector::new();
        let mut fut = UpdateManager::<_, _, _, StateChangeCollector, _, FakeAttemptNotifier>::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            checker.clone(),
            UnreachableUpdateApplier,
            FakeCommitQuerier::new(),
        )
        .boxed();
        let mut manager = match executor.run_until_stalled(&mut fut) {
            Poll::Ready(manager) => manager,
            Poll::Pending => panic!("manager not ready"),
        }
        .spawn();

        let period = 24.hours();
        let config = ConfigBuilder::new().poll_frequency(period).build();
        let mut cron = run_periodic_update_check(manager.clone(), &config).boxed();

        // Let the cron task set up the timer, but nothing interesting happens until time advances.
        assert_eq!(Poll::Pending, executor.run_until_stalled(&mut cron));

        // User wins, and only 1 update check happens.
        let options = CheckOptions::builder().initiator(Initiator::User).build();
        let mut fut = manager.try_start_update(options, Some(callback.clone())).boxed();
        assert_eq!(executor.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
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
            vec![State::CheckingForUpdates, State::NoUpdateAvailable]
        );
    }
}
