// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app_set::{AppSet, AppSetExt as _},
    configuration::Config,
    cup_ecdsa::Cupv2Handler,
    http_request::HttpRequest,
    installer::{Installer, Plan},
    metrics::MetricsReporter,
    policy::PolicyEngine,
    request_builder::RequestParams,
    state_machine::{update_check, ControlHandle, StateMachine, StateMachineEvent},
    storage::Storage,
    time::Timer,
};
use futures::{channel::mpsc, lock::Mutex, prelude::*};
use std::rc::Rc;

#[cfg(test)]
use crate::{
    app_set::VecAppSet,
    common::App,
    cup_ecdsa::test_support::MockCupv2Handler,
    http_request::StubHttpRequest,
    installer::stub::{StubInstaller, StubPlan},
    metrics::StubMetricsReporter,
    policy::StubPolicyEngine,
    state_machine::{RebootAfterUpdate, UpdateCheckError},
    storage::StubStorage,
    time::{timers::StubTimer, MockTimeSource},
};

/// Helper type to build/start a [`StateMachine`].
#[derive(Debug)]
pub struct StateMachineBuilder<PE, HR, IN, TM, MR, ST, AS, CH>
where
    PE: PolicyEngine,
    HR: HttpRequest,
    IN: Installer,
    TM: Timer,
    MR: MetricsReporter,
    ST: Storage,
    AS: AppSet,
    CH: Cupv2Handler,
{
    policy_engine: PE,
    http: HR,
    installer: IN,
    timer: TM,
    metrics_reporter: MR,
    storage: Rc<Mutex<ST>>,
    config: Config,
    app_set: Rc<Mutex<AS>>,
    cup_handler: Option<CH>,
}

impl<'a, PE, HR, IN, TM, MR, ST, AS, CH> StateMachineBuilder<PE, HR, IN, TM, MR, ST, AS, CH>
where
    PE: 'a + PolicyEngine,
    HR: 'a + HttpRequest,
    IN: 'a + Installer,
    TM: 'a + Timer,
    MR: 'a + MetricsReporter,
    ST: 'a + Storage,
    AS: 'a + AppSet,
    CH: 'a + Cupv2Handler,
{
    /// Creates a new `StateMachineBuilder` using the given trait implementations.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        policy_engine: PE,
        http: HR,
        installer: IN,
        timer: TM,
        metrics_reporter: MR,
        storage: Rc<Mutex<ST>>,
        config: Config,
        app_set: Rc<Mutex<AS>>,
        cup_handler: Option<CH>,
    ) -> Self {
        Self {
            policy_engine,
            http,
            installer,
            timer,
            metrics_reporter,
            storage,
            config,
            app_set,
            cup_handler,
        }
    }
}

impl<'a, PE, HR, IN, TM, MR, ST, AS, CH> StateMachineBuilder<PE, HR, IN, TM, MR, ST, AS, CH>
where
    PE: 'a + PolicyEngine,
    HR: 'a + HttpRequest,
    IN: 'a + Installer,
    TM: 'a + Timer,
    MR: 'a + MetricsReporter,
    ST: 'a + Storage,
    AS: 'a + AppSet,
    CH: 'a + Cupv2Handler,
{
    /// Configures the state machine to use the provided policy_engine implementation.
    pub fn policy_engine<PE2: 'a + PolicyEngine>(
        self,
        policy_engine: PE2,
    ) -> StateMachineBuilder<PE2, HR, IN, TM, MR, ST, AS, CH> {
        StateMachineBuilder {
            policy_engine,
            http: self.http,
            installer: self.installer,
            timer: self.timer,
            metrics_reporter: self.metrics_reporter,
            storage: self.storage,
            config: self.config,
            app_set: self.app_set,
            cup_handler: self.cup_handler,
        }
    }

    /// Configures the state machine to use the provided http implementation.
    pub fn http<HR2: 'a + HttpRequest>(
        self,
        http: HR2,
    ) -> StateMachineBuilder<PE, HR2, IN, TM, MR, ST, AS, CH> {
        StateMachineBuilder {
            policy_engine: self.policy_engine,
            http,
            installer: self.installer,
            timer: self.timer,
            metrics_reporter: self.metrics_reporter,
            storage: self.storage,
            config: self.config,
            app_set: self.app_set,
            cup_handler: self.cup_handler,
        }
    }

    /// Configures the state machine to use the provided installer implementation.
    pub fn installer<IN2: 'a + Installer>(
        self,
        installer: IN2,
    ) -> StateMachineBuilder<PE, HR, IN2, TM, MR, ST, AS, CH> {
        StateMachineBuilder {
            policy_engine: self.policy_engine,
            http: self.http,
            installer,
            timer: self.timer,
            metrics_reporter: self.metrics_reporter,
            storage: self.storage,
            config: self.config,
            app_set: self.app_set,
            cup_handler: self.cup_handler,
        }
    }

    /// Configures the state machine to use the provided timer implementation.
    pub fn timer<TM2: 'a + Timer>(
        self,
        timer: TM2,
    ) -> StateMachineBuilder<PE, HR, IN, TM2, MR, ST, AS, CH> {
        StateMachineBuilder {
            policy_engine: self.policy_engine,
            http: self.http,
            installer: self.installer,
            timer,
            metrics_reporter: self.metrics_reporter,
            storage: self.storage,
            config: self.config,
            app_set: self.app_set,
            cup_handler: self.cup_handler,
        }
    }

    /// Configures the state machine to use the provided metrics_reporter implementation.
    pub fn metrics_reporter<MR2: 'a + MetricsReporter>(
        self,
        metrics_reporter: MR2,
    ) -> StateMachineBuilder<PE, HR, IN, TM, MR2, ST, AS, CH> {
        StateMachineBuilder {
            policy_engine: self.policy_engine,
            http: self.http,
            installer: self.installer,
            timer: self.timer,
            metrics_reporter,
            storage: self.storage,
            config: self.config,
            app_set: self.app_set,
            cup_handler: self.cup_handler,
        }
    }

    /// Configures the state machine to use the provided storage implementation.
    pub fn storage<ST2: 'a + Storage>(
        self,
        storage: Rc<Mutex<ST2>>,
    ) -> StateMachineBuilder<PE, HR, IN, TM, MR, ST2, AS, CH> {
        StateMachineBuilder {
            policy_engine: self.policy_engine,
            http: self.http,
            installer: self.installer,
            timer: self.timer,
            metrics_reporter: self.metrics_reporter,
            storage,
            config: self.config,
            app_set: self.app_set,
            cup_handler: self.cup_handler,
        }
    }

    /// Configures the state machine to use the provided config.
    pub fn config(mut self, config: Config) -> Self {
        self.config = config;
        self
    }

    /// Configures the state machine to use the provided app_set implementation.
    pub fn app_set<AS2: 'a + AppSet>(
        self,
        app_set: Rc<Mutex<AS2>>,
    ) -> StateMachineBuilder<PE, HR, IN, TM, MR, ST, AS2, CH> {
        StateMachineBuilder {
            policy_engine: self.policy_engine,
            http: self.http,
            installer: self.installer,
            timer: self.timer,
            metrics_reporter: self.metrics_reporter,
            storage: self.storage,
            config: self.config,
            app_set,
            cup_handler: self.cup_handler,
        }
    }

    pub fn cup_handler<CH2: 'a + Cupv2Handler>(
        self,
        cup_handler: Option<CH2>,
    ) -> StateMachineBuilder<PE, HR, IN, TM, MR, ST, AS, CH2> {
        StateMachineBuilder {
            policy_engine: self.policy_engine,
            http: self.http,
            installer: self.installer,
            timer: self.timer,
            metrics_reporter: self.metrics_reporter,
            storage: self.storage,
            config: self.config,
            app_set: self.app_set,
            cup_handler,
        }
    }
}

impl<'a, PE, HR, IN, TM, MR, ST, AS, CH, IR, PL> StateMachineBuilder<PE, HR, IN, TM, MR, ST, AS, CH>
where
    PE: 'a + PolicyEngine<InstallResult = IR, InstallPlan = PL>,
    HR: 'a + HttpRequest,
    IN: 'a + Installer<InstallResult = IR, InstallPlan = PL>,
    TM: 'a + Timer,
    MR: 'a + MetricsReporter,
    ST: 'a + Storage,
    AS: 'a + AppSet,
    CH: 'a + Cupv2Handler,
    IR: 'static + Send,
    PL: 'a + Plan,
{
    pub async fn build(self) -> StateMachine<PE, HR, IN, TM, MR, ST, AS, CH> {
        let StateMachineBuilder {
            policy_engine,
            http,
            installer,
            timer,
            metrics_reporter,
            storage,
            config,
            app_set,
            cup_handler,
        } = self;

        let ((), context) = {
            let storage = storage.lock().await;
            let mut app_set = app_set.lock().await;
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
            app_set,
            cup_handler,
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
    pub async fn oneshot_check(self) -> impl Stream<Item = StateMachineEvent> + 'a {
        let mut state_machine = self.build().await;
        let request_params = RequestParams::default();

        async_generator::generate(move |mut co| async move {
            state_machine.start_update_check(request_params, &mut co).await;
        })
        .into_yielded()
    }

    /// Run perform_update_check once, returning the update check result.
    #[cfg(test)]
    pub(super) async fn oneshot(
        self,
        request_params: RequestParams,
    ) -> Result<(update_check::Response, RebootAfterUpdate<IR>), UpdateCheckError> {
        self.build().await.oneshot(request_params).await
    }
}

#[cfg(test)]
impl
    StateMachineBuilder<
        StubPolicyEngine<StubPlan, MockTimeSource>,
        StubHttpRequest,
        StubInstaller,
        StubTimer,
        StubMetricsReporter,
        StubStorage,
        VecAppSet,
        MockCupv2Handler,
    >
{
    /// Create a new StateMachine with stub implementations and configuration.
    pub fn new_stub() -> Self {
        let config = crate::configuration::test_support::config_generator();

        let app_set = VecAppSet::new(vec![App::builder(
            "{00000000-0000-0000-0000-000000000001}",
            [1, 2, 3, 4],
        )
        .with_cohort(crate::protocol::Cohort::new("stable-channel"))
        .build()]);
        let mock_time = MockTimeSource::new_from_now();

        StateMachineBuilder::new(
            StubPolicyEngine::new(mock_time),
            StubHttpRequest,
            StubInstaller::default(),
            StubTimer,
            StubMetricsReporter,
            Rc::new(Mutex::new(StubStorage)),
            config,
            Rc::new(Mutex::new(app_set)),
            Some(MockCupv2Handler::new()),
        )
    }
}
