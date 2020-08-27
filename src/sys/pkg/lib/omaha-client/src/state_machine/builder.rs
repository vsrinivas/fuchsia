// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common::{AppSet, CheckOptions},
    configuration::Config,
    http_request::HttpRequest,
    installer::Installer,
    metrics::MetricsReporter,
    policy::PolicyEngine,
    state_machine::{update_check, ControlHandle, State, StateMachine, StateMachineEvent},
    storage::Storage,
    time::Timer,
};
use futures::{channel::mpsc, lock::Mutex, prelude::*};
use std::rc::Rc;

#[cfg(test)]
use crate::{
    common::App,
    http_request::StubHttpRequest,
    installer::stub::StubInstaller,
    metrics::StubMetricsReporter,
    policy::StubPolicyEngine,
    state_machine::UpdateCheckError,
    storage::StubStorage,
    time::{timers::StubTimer, MockTimeSource},
};

/// Helper type to build/start a [`StateMachine`].
#[derive(Debug)]
pub struct StateMachineBuilder<PE, HR, IN, TM, MR, ST>
where
    PE: PolicyEngine,
    HR: HttpRequest,
    IN: Installer,
    TM: Timer,
    MR: MetricsReporter,
    ST: Storage,
{
    policy_engine: PE,
    http: HR,
    installer: IN,
    timer: TM,
    metrics_reporter: MR,
    storage: Rc<Mutex<ST>>,
    config: Config,
    app_set: AppSet,
}

impl<'a, PE, HR, IN, TM, MR, ST> StateMachineBuilder<PE, HR, IN, TM, MR, ST>
where
    PE: 'a + PolicyEngine,
    HR: 'a + HttpRequest,
    IN: 'a + Installer,
    TM: 'a + Timer,
    MR: 'a + MetricsReporter,
    ST: 'a + Storage,
{
    /// Creates a new `StateMachineBuilder` using the given trait implementations.
    pub fn new(
        policy_engine: PE,
        http: HR,
        installer: IN,
        timer: TM,
        metrics_reporter: MR,
        storage: Rc<Mutex<ST>>,
        config: Config,
        app_set: AppSet,
    ) -> Self {
        Self { policy_engine, http, installer, timer, metrics_reporter, storage, config, app_set }
    }

    /// Configures the state machine to use the provided policy_engine implementation.
    pub fn policy_engine<PE2: 'a + PolicyEngine>(
        self,
        policy_engine: PE2,
    ) -> StateMachineBuilder<PE2, HR, IN, TM, MR, ST> {
        StateMachineBuilder {
            policy_engine,
            http: self.http,
            installer: self.installer,
            timer: self.timer,
            metrics_reporter: self.metrics_reporter,
            storage: self.storage,
            config: self.config,
            app_set: self.app_set,
        }
    }

    /// Configures the state machine to use the provided http implementation.
    pub fn http<HR2: 'a + HttpRequest>(
        self,
        http: HR2,
    ) -> StateMachineBuilder<PE, HR2, IN, TM, MR, ST> {
        StateMachineBuilder {
            policy_engine: self.policy_engine,
            http,
            installer: self.installer,
            timer: self.timer,
            metrics_reporter: self.metrics_reporter,
            storage: self.storage,
            config: self.config,
            app_set: self.app_set,
        }
    }

    /// Configures the state machine to use the provided installer implementation.
    pub fn installer<IN2: 'a + Installer>(
        self,
        installer: IN2,
    ) -> StateMachineBuilder<PE, HR, IN2, TM, MR, ST> {
        StateMachineBuilder {
            policy_engine: self.policy_engine,
            http: self.http,
            installer,
            timer: self.timer,
            metrics_reporter: self.metrics_reporter,
            storage: self.storage,
            config: self.config,
            app_set: self.app_set,
        }
    }

    /// Configures the state machine to use the provided timer implementation.
    pub fn timer<TM2: 'a + Timer>(
        self,
        timer: TM2,
    ) -> StateMachineBuilder<PE, HR, IN, TM2, MR, ST> {
        StateMachineBuilder {
            policy_engine: self.policy_engine,
            http: self.http,
            installer: self.installer,
            timer,
            metrics_reporter: self.metrics_reporter,
            storage: self.storage,
            config: self.config,
            app_set: self.app_set,
        }
    }

    /// Configures the state machine to use the provided metrics_reporter implementation.
    pub fn metrics_reporter<MR2: 'a + MetricsReporter>(
        self,
        metrics_reporter: MR2,
    ) -> StateMachineBuilder<PE, HR, IN, TM, MR2, ST> {
        StateMachineBuilder {
            policy_engine: self.policy_engine,
            http: self.http,
            installer: self.installer,
            timer: self.timer,
            metrics_reporter,
            storage: self.storage,
            config: self.config,
            app_set: self.app_set,
        }
    }

    /// Configures the state machine to use the provided storage implementation.
    pub fn storage<ST2: 'a + Storage>(
        self,
        storage: Rc<Mutex<ST2>>,
    ) -> StateMachineBuilder<PE, HR, IN, TM, MR, ST2> {
        StateMachineBuilder {
            policy_engine: self.policy_engine,
            http: self.http,
            installer: self.installer,
            timer: self.timer,
            metrics_reporter: self.metrics_reporter,
            storage,
            config: self.config,
            app_set: self.app_set,
        }
    }

    /// Configures the state machine to use the provided config.
    pub fn config(mut self, config: Config) -> Self {
        self.config = config;
        self
    }

    /// Configures the state machine to use the provided app_set.
    pub fn app_set(mut self, app_set: AppSet) -> Self {
        self.app_set = app_set;
        self
    }

    pub async fn build(self) -> StateMachine<PE, HR, IN, TM, MR, ST> {
        let StateMachineBuilder {
            policy_engine,
            http,
            installer,
            timer,
            metrics_reporter,
            storage,
            config,
            mut app_set,
        } = self;

        let ((), context) = {
            let storage = storage.lock().await;
            futures::join!(app_set.load(&*storage), update_check::Context::load(&*storage))
        };

        let time_source = policy_engine.time_source().clone();

        StateMachine {
            config,
            policy_engine,
            http,
            installer,
            timer,
            time_source,
            metrics_reporter,
            storage_ref: storage,
            context,
            state: State::Idle,
            app_set,
        }
    }

    /// Start the StateMachine to do periodic update checks in the background or when requested
    /// through the returned control handle.  The returned stream must be polled to make
    /// forward progress.
    // TODO: find a better name for this function.
    pub async fn start(self) -> (ControlHandle, impl Stream<Item = StateMachineEvent> + 'a) {
        let state_machine = self.build().await;

        let (send, recv) = mpsc::channel(0);
        (
            ControlHandle(send),
            async_generator::generate(move |co| state_machine.run(recv, co)).into_yielded(),
        )
    }

    /// Run start_upate_check once, returning a stream of the states it produces.
    pub async fn oneshot_check(
        self,
        options: CheckOptions,
    ) -> impl Stream<Item = StateMachineEvent> + 'a {
        let mut state_machine = self.build().await;

        async_generator::generate(move |mut co| async move {
            state_machine.start_update_check(&options, &mut co).await
        })
        .into_yielded()
    }

    /// Run perform_update_check once, returning the update check result.
    #[cfg(test)]
    pub async fn oneshot(self) -> Result<update_check::Response, UpdateCheckError> {
        self.build().await.oneshot().await
    }
}

#[cfg(test)]
impl
    StateMachineBuilder<
        StubPolicyEngine<MockTimeSource>,
        StubHttpRequest,
        StubInstaller,
        StubTimer,
        StubMetricsReporter,
        StubStorage,
    >
{
    /// Create a new StateMachine with stub implementations and configuration.
    pub fn new_stub() -> Self {
        let config = crate::configuration::test_support::config_generator();

        let app_set =
            AppSet::new(vec![App::builder("{00000000-0000-0000-0000-000000000001}", [1, 2, 3, 4])
                .with_cohort(crate::protocol::Cohort::new("stable-channel"))
                .build()]);
        let mock_time = MockTimeSource::new_from_now();

        Self::new(
            StubPolicyEngine::new(mock_time),
            StubHttpRequest,
            StubInstaller::default(),
            StubTimer,
            StubMetricsReporter,
            Rc::new(Mutex::new(StubStorage)),
            config,
            app_set,
        )
    }
}
