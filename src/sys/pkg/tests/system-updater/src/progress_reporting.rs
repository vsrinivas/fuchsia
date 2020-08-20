// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    fidl_fuchsia_update_installer_ext::{
        monitor_update, start_update, Initiator, Options, Progress, State, StateId,
        UpdateAttemptError, UpdateInfo, UpdateInfoAndProgress,
    },
    fuchsia_url::pkg_url::PkgUrl,
    matches::assert_matches,
    pretty_assertions::assert_eq,
};

#[fasync::run_singlethreaded(test)]
async fn progress_reporting_fetch_multiple_packages() {
    let env = TestEnv::builder().build();

    let pkg1_url = pinned_pkg_url!("package1/0", "aa");
    let pkg2_url = pinned_pkg_url!("package2/0", "bb");
    let pkg3_url = pinned_pkg_url!("package3/0", "cc");

    let update_pkg = env
        .resolver
        .package("update", UPDATE_HASH)
        .add_file("packages.json", make_packages_json([pkg1_url, pkg2_url, pkg3_url]));
    let pkg1 = env.resolver.package("package1", merkle_str!("aa"));
    let pkg2 = env.resolver.package("package2", merkle_str!("bb"));
    let pkg3 = env.resolver.package("package3", merkle_str!("cc"));

    // We need to block all the resolves so that we can assert Fetch progress
    // is emitted for each pkg fetch. Otherwise, the Fetch state updates could merge
    // into one Fetch state.
    let handle_update_pkg = env.resolver.url(UPDATE_PKG_URL).block_once();
    let handle_pkg1 = env.resolver.url(pkg1_url).block_once();
    let handle_pkg2 = env.resolver.url(pkg2_url).block_once();
    let handle_pkg3 = env.resolver.url(pkg3_url).block_once();

    // Start the system update.
    let installer_proxy = env.installer_proxy();
    let mut attempt =
        start_update(&UPDATE_PKG_URL.parse().unwrap(), default_options(), &installer_proxy, None)
            .await
            .unwrap();

    assert_eq!(attempt.next().await.unwrap().unwrap(), State::Prepare);

    let info = UpdateInfo::builder().download_size(0).build();
    handle_update_pkg.resolve(&update_pkg).await;
    assert_eq!(
        attempt.next().await.unwrap().unwrap(),
        State::Fetch(
            UpdateInfoAndProgress::builder()
                .info(info.clone())
                .progress(Progress::builder().fraction_completed(0.0).bytes_downloaded(0).build())
                .build()
        )
    );

    handle_pkg1.resolve(&pkg1).await;
    assert_eq!(
        attempt.next().await.unwrap().unwrap(),
        State::Fetch(
            UpdateInfoAndProgress::builder()
                .info(info.clone())
                .progress(Progress::builder().fraction_completed(0.25).bytes_downloaded(0).build())
                .build()
        )
    );

    handle_pkg2.resolve(&pkg2).await;
    assert_eq!(
        attempt.next().await.unwrap().unwrap(),
        State::Fetch(
            UpdateInfoAndProgress::builder()
                .info(info.clone())
                .progress(Progress::builder().fraction_completed(0.5).bytes_downloaded(0).build())
                .build()
        )
    );

    handle_pkg3.resolve(&pkg3).await;
    assert_eq!(
        attempt.next().await.unwrap().unwrap(),
        State::Fetch(
            UpdateInfoAndProgress::builder()
                .info(info.clone())
                .progress(Progress::builder().fraction_completed(0.75).bytes_downloaded(0).build())
                .build()
        )
    );

    // In this test, we are only testing Fetch updates. Let's assert the Fetch
    // phase is over.
    assert_eq!(attempt.next().await.unwrap().unwrap().id(), StateId::Stage);
}

#[fasync::run_singlethreaded(test)]
async fn monitor_fails_when_no_update_running() {
    let env = TestEnv::builder().build();

    // There is no update underway, so the monitor should not attach.
    assert_matches!(monitor_update(None, &env.installer_proxy()).await, Ok(None));
    assert_eq!(env.logger_factory.loggers.lock().len(), 0);
    assert_eq!(env.take_interactions(), vec![]);
}

#[fasync::run_singlethreaded(test)]
async fn monitor_connects_to_existing_attempt() {
    let env = TestEnv::builder().build();

    let update_pkg = env
        .resolver
        .package("update", UPDATE_HASH)
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi");

    // Block the update pkg resolve to ensure the update attempt is still in
    // in progress when we try to attach a monitor.
    let handle_update_pkg = env.resolver.url(UPDATE_PKG_URL).block_once();

    // Start the system update.
    let installer_proxy = env.installer_proxy();
    let attempt0 =
        start_update(&UPDATE_PKG_URL.parse().unwrap(), default_options(), &installer_proxy, None)
            .await
            .unwrap();

    // Attach monitor.
    let attempt1 =
        monitor_update(Some(attempt0.attempt_id()), &installer_proxy).await.unwrap().unwrap();

    // Now that we attached both monitors to the current attempt, we can unblock the
    // resolve and resume the update attempt.
    handle_update_pkg.resolve(&update_pkg).await;
    let monitor0_events: Vec<State> = attempt0.map(|res| res.unwrap()).collect().await;
    let monitor1_events: Vec<State> = attempt1.map(|res| res.unwrap()).collect().await;

    // Since we wait until the update attempt is over to read from monitor1 events,
    // we should expect that the events in monitor1 merged.
    assert_eq!(monitor1_events.len(), 5);

    // While the number of events are different, the ordering should still be the same.
    let expected_order =
        [StateId::Prepare, StateId::Fetch, StateId::Stage, StateId::WaitToReboot, StateId::Reboot];
    assert_success_monitor_states(monitor0_events, &expected_order);
    assert_success_monitor_states(monitor1_events, &expected_order);
}

#[fasync::run_singlethreaded(test)]
async fn succeed_additional_start_requests_when_compatible() {
    let env = TestEnv::builder().build();

    let update_pkg = env
        .resolver
        .package("update", UPDATE_HASH)
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi");

    // Block the update pkg resolve to ensure the update attempt is still in
    // in progress when we try to attach a monitor.
    let handle_update_pkg = env.resolver.url(UPDATE_PKG_URL).block_once();

    // Start the system update, making 2 start_update requests. The second start_update request
    // is essentially just a monitor_update request in this case.
    let installer_proxy = env.installer_proxy();
    let url: PkgUrl = UPDATE_PKG_URL.parse().unwrap();
    let attempt0 = start_update(&url, default_options(), &installer_proxy, None).await.unwrap();
    let attempt1 = start_update(
        &url,
        Options {
            initiator: Initiator::User,
            allow_attach_to_existing_attempt: true,
            should_write_recovery: true,
        },
        &installer_proxy,
        None,
    )
    .await
    .unwrap();

    // Now that we attached both monitors to the current attempt, we can unblock the
    // resolve and resume the update attempt.
    handle_update_pkg.resolve(&update_pkg).await;
    let monitor0_events: Vec<State> = attempt0.map(|res| res.unwrap()).collect().await;
    let monitor1_events: Vec<State> = attempt1.map(|res| res.unwrap()).collect().await;

    // Since we waited for the update attempt to complete before reading from monitor1,
    // the events in monitor1 should have merged.
    assert_eq!(monitor1_events.len(), 5);

    // While the number of events are different, the ordering should still be the same.
    let expected_order =
        [StateId::Prepare, StateId::Fetch, StateId::Stage, StateId::WaitToReboot, StateId::Reboot];
    assert_success_monitor_states(monitor0_events, &expected_order);
    assert_success_monitor_states(monitor1_events, &expected_order);
}

#[fasync::run_singlethreaded(test)]
async fn fail_additional_start_requests_when_not_compatible() {
    let env = TestEnv::builder().build();

    env.resolver
        .package("update", UPDATE_HASH)
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi");

    // Block the update pkg resolve to ensure the update attempt is still in
    // in progress when we try to make additional start_update requests.
    let _handle_update_pkg = env.resolver.url(UPDATE_PKG_URL).block_once();

    // Start the system update.
    let installer_proxy = env.installer_proxy();
    let compatible_url: PkgUrl = UPDATE_PKG_URL.parse().unwrap();
    let compatible_options = Options {
        initiator: Initiator::User,
        allow_attach_to_existing_attempt: true,
        should_write_recovery: true,
    };
    let _attempt =
        start_update(&compatible_url, compatible_options.clone(), &installer_proxy, None)
            .await
            .unwrap();

    // Define incompatible options and url.
    let incompatible_options0 = Options {
        initiator: Initiator::User,
        allow_attach_to_existing_attempt: true,
        should_write_recovery: false,
    };
    let incompatible_options1 = Options {
        initiator: Initiator::User,
        allow_attach_to_existing_attempt: false,
        should_write_recovery: true,
    };
    let incompatible_url = "fuchsia-pkg://fuchsia.com/different-url".parse().unwrap();

    // Show that start_update requests fail with AlreadyInProgress errors.
    assert_matches!(
        start_update(&compatible_url, incompatible_options0, &installer_proxy, None)
            .await
            .map(|_| ())
            .unwrap_err(),
        UpdateAttemptError::InstallInProgress
    );
    assert_matches!(
        start_update(&compatible_url, incompatible_options1, &installer_proxy, None)
            .await
            .map(|_| ())
            .unwrap_err(),
        UpdateAttemptError::InstallInProgress
    );
    assert_matches!(
        start_update(&incompatible_url, compatible_options.clone(), &installer_proxy, None)
            .await
            .map(|_| ())
            .unwrap_err(),
        UpdateAttemptError::InstallInProgress
    );
    let (_, server_end) = fidl::endpoints::create_endpoints().unwrap();
    assert_matches!(
        start_update(&compatible_url, compatible_options, &installer_proxy, Some(server_end))
            .await
            .map(|_| ())
            .unwrap_err(),
        UpdateAttemptError::InstallInProgress
    );
}

pub fn assert_success_monitor_states(states: Vec<State>, ordering: &[StateId]) {
    let res = util::verify_monitor_states(&states, &ordering, false);
    if let Err(e) = res {
        panic!("Error received when verifying monitor states: {:#}\nWant ordering: {:#?}\nGot states:{:#?}", e, ordering, states);
    }
}

pub fn _assert_failure_monitor_states(states: Vec<State>, ordering: Vec<StateId>) {
    let res = util::verify_monitor_states(&states, &ordering, false);
    if let Err(e) = res {
        panic!("Error received when verifying monitor states: {:#}\nWant ordering: {:#?}\nGot states:{:#?}", e, ordering, states);
    }
}

mod util {
    use {
        fidl_fuchsia_update_installer_ext::{
            Progress, State, StateId, UpdateInfo, UpdateInfoAndProgress,
        },
        std::collections::HashSet,
        thiserror::Error,
    };

    #[derive(Debug, Error, PartialEq)]
    pub enum VerifyMonitorStatesError {
        #[error("there are more IDs in the ordering than in the provided states")]
        TooFewStates,

        #[error("the order of the states does not match the passed in ordering")]
        OutOfOrder,

        #[error("progress should be strictly nondecreasing")]
        ProgressDecreased,

        #[error("received a {0:?} state, which wasn't in the ordering")]
        UnexpectedState(StateId),

        #[error("the final fraction_completed should be 1.0 on successful attempts")]
        FractionCompletedSuccessNot1,
    }

    /// Validate that
    /// * states are in the right order (ignoring duplicates)
    /// * progress is strictly nondecreasing
    /// * fraction_completed stays within [0.0, 1,0] bounds
    /// * on successful update attempts, the final progress should be 1.0.
    pub fn verify_monitor_states(
        states: &[State],
        ordering: &[StateId],
        expect_success: bool,
    ) -> Result<(), VerifyMonitorStatesError> {
        // Sanity check input
        if states.len() < ordering.len() {
            return Err(VerifyMonitorStatesError::TooFewStates);
        }
        let ordering_set: HashSet<StateId> = ordering.iter().cloned().collect();
        if ordering_set.len() != ordering.len() {
            panic!("Ordering should not have duplicates: {:?} ", ordering);
        }
        for state in states.iter() {
            if !ordering.contains(&state.id()) {
                return Err(VerifyMonitorStatesError::UnexpectedState(state.id()));
            }
        }

        let mut prev_fraction_completed = 0.0;
        let mut state_index = 0;
        for ordering_index in 0..ordering.len() {
            // Check if it's out of order.
            if states[state_index].id() != ordering[ordering_index] {
                return Err(VerifyMonitorStatesError::OutOfOrder);
            }

            // Check progress.
            while state_index < states.len() && states[state_index].id() == ordering[ordering_index]
            {
                if let Some(progress) = states[state_index].progress() {
                    // Verify we aren't decreasing.
                    if progress.fraction_completed() < prev_fraction_completed {
                        return Err(VerifyMonitorStatesError::ProgressDecreased);
                    }
                    prev_fraction_completed = progress.fraction_completed();
                }
                state_index += 1;
            }
        }

        // The last progress should be 1.0 on success.
        if expect_success {
            let states_with_full_fraction_completion = states.iter().find(|state| {
                if let Some(progress) = state.progress() {
                    return progress.fraction_completed() == 1.0;
                }
                false
            });
            if states_with_full_fraction_completion.is_none() {
                return Err(VerifyMonitorStatesError::FractionCompletedSuccessNot1);
            }
        }

        Ok(())
    }

    #[test]
    fn fail_too_few_states() {
        let states = vec![State::Prepare];
        let ordering = vec![StateId::Prepare, StateId::Stage];

        assert_eq!(
            verify_monitor_states(&states, &ordering, true),
            Err(VerifyMonitorStatesError::TooFewStates)
        );
    }

    #[test]
    #[should_panic]
    fn fail_duplicates_in_ordering() {
        let states = vec![State::Prepare, State::Prepare];
        let ordering = vec![StateId::Prepare, StateId::Prepare];

        verify_monitor_states(&states, &ordering, true).unwrap();
    }

    #[test]
    fn fail_unexpected_state() {
        let states = vec![State::Prepare, State::FailPrepare];
        let ordering = vec![StateId::Prepare];

        assert_eq!(
            verify_monitor_states(&states, &ordering, true),
            Err(VerifyMonitorStatesError::UnexpectedState(StateId::FailPrepare))
        );
    }

    #[test]
    fn fail_out_of_order() {
        let states = vec![State::Prepare, State::FailPrepare];
        let ordering = vec![StateId::FailPrepare, StateId::Prepare];

        assert_eq!(
            verify_monitor_states(&states, &ordering, true),
            Err(VerifyMonitorStatesError::OutOfOrder)
        );
    }

    #[test]
    fn fail_progress_decreased() {
        let info = UpdateInfo::builder().download_size(0).build();
        let d0 = UpdateInfoAndProgress::builder()
            .info(info.clone())
            .progress(Progress::builder().fraction_completed(0.0).bytes_downloaded(0).build())
            .build();
        let d1 = UpdateInfoAndProgress::builder()
            .info(info.clone())
            .progress(Progress::builder().fraction_completed(0.4).bytes_downloaded(0).build())
            .build();
        let d2 = UpdateInfoAndProgress::builder()
            .info(info)
            .progress(Progress::builder().fraction_completed(0.2).bytes_downloaded(0).build())
            .build();
        let states = vec![State::Prepare, State::Fetch(d0), State::Fetch(d1), State::Fetch(d2)];
        let ordering = vec![StateId::Prepare, StateId::Fetch];

        assert_eq!(
            verify_monitor_states(&states, &ordering, true),
            Err(VerifyMonitorStatesError::ProgressDecreased)
        );
    }

    #[test]
    fn fail_fraction_completed_should_end_with_1_on_success() {
        let info = UpdateInfo::builder().download_size(0).build();
        let d0 = UpdateInfoAndProgress::builder()
            .info(info.clone())
            .progress(Progress::builder().fraction_completed(0.0).bytes_downloaded(0).build())
            .build();
        let d1 = UpdateInfoAndProgress::builder()
            .info(info)
            .progress(Progress::builder().fraction_completed(0.9).bytes_downloaded(0).build())
            .build();
        let states = vec![State::Prepare, State::Fetch(d0), State::Fetch(d1)];
        let ordering = vec![StateId::Prepare, StateId::Fetch];

        assert_eq!(
            verify_monitor_states(&states, &ordering, true),
            Err(VerifyMonitorStatesError::FractionCompletedSuccessNot1)
        );
        // Sanity check failure method doesn't care if last fraction completed is not 1.0.
        assert_eq!(verify_monitor_states(&states, &ordering, false), Ok(()));
    }

    #[test]
    fn success() {
        let info = UpdateInfo::builder().download_size(0).build();
        let d0 = UpdateInfoAndProgress::builder()
            .info(info.clone())
            .progress(Progress::builder().fraction_completed(0.0).bytes_downloaded(0).build())
            .build();
        let d1 = UpdateInfoAndProgress::builder()
            .info(info.clone())
            .progress(Progress::builder().fraction_completed(0.5).bytes_downloaded(0).build())
            .build();
        let d2 = UpdateInfoAndProgress::builder()
            .info(info)
            .progress(Progress::builder().fraction_completed(1.0).bytes_downloaded(0).build())
            .build();
        let states = vec![
            State::Prepare,
            State::Fetch(d0),
            State::Fetch(d1.clone()),
            State::Stage(d1),
            State::Stage(d2.clone()),
            State::Reboot(d2),
        ];
        let ordering = vec![StateId::Prepare, StateId::Fetch, StateId::Stage, StateId::Reboot];

        assert_eq!(verify_monitor_states(&states, &ordering, true), Ok(()));
    }
}
