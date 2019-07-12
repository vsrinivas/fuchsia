// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::apply::Initiator;
use crate::manager_manager::{
    ManagerManager, RealUpdateApplier, RealUpdateChecker, State, StateChangeCallback,
    UpdateApplier, UpdateChecker,
};
use failure::{bail, Error, ResultExt};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_update::{
    ManagerCheckNowResponder, ManagerControlHandle, ManagerGetStateResponder, ManagerRequest,
    ManagerRequestStream, MonitorControlHandle, MonitorMarker,
};
use futures::prelude::*;
use std::sync::Arc;

pub type RealManagerService = ManagerService<RealUpdateChecker, RealUpdateApplier>;
pub type RealStateChangeCallback = MonitorControlHandle;
pub type RealManagerManager =
    ManagerManager<RealUpdateChecker, RealUpdateApplier, RealStateChangeCallback>;

#[derive(Clone)]
pub struct ManagerService<C, A>
where
    C: UpdateChecker,
    A: UpdateApplier,
{
    manager_manager: Arc<ManagerManager<C, A, RealStateChangeCallback>>,
}

impl<C, A> ManagerService<C, A>
where
    C: UpdateChecker,
    A: UpdateApplier,
{
    pub fn new_manager_and_service() -> (Arc<RealManagerManager>, RealManagerService) {
        let manager_manager = Arc::new(ManagerManager::from_checker_and_applier(
            RealUpdateChecker,
            RealUpdateApplier,
        ));
        let manager_service = RealManagerService { manager_manager: Arc::clone(&manager_manager) };
        (manager_manager, manager_service)
    }

    pub async fn handle_request_stream(
        &self,
        mut request_stream: ManagerRequestStream,
    ) -> Result<(), Error> {
        while let Some(event) =
            await!(request_stream.try_next()).context("error extracting request from stream")?
        {
            match event {
                ManagerRequest::CheckNow { options, monitor, responder } => {
                    self.handle_check_now(options, monitor, responder)?;
                }
                ManagerRequest::GetState { responder } => {
                    self.handle_get_state(responder)?;
                }
                ManagerRequest::AddMonitor { monitor, control_handle } => {
                    self.handle_add_monitor(monitor, control_handle)?;
                }
            }
        }
        Ok(())
    }

    fn handle_check_now(
        &self,
        options: fidl_fuchsia_update::Options,
        monitor: Option<ServerEnd<MonitorMarker>>,
        responder: ManagerCheckNowResponder,
    ) -> Result<(), Error> {
        let initiator = extract_initiator(&options)?;
        let monitor_control_handle = if let Some(monitor) = monitor {
            // Drop stream b/c Monitor protocol has no client methods
            let (_stream, control_handle) =
                monitor.into_stream_and_control_handle().context("split CheckNow ServerEnd")?;
            Some(control_handle)
        } else {
            None
        };
        responder
            .send(self.manager_manager.try_start_update(initiator, monitor_control_handle))
            .context("error sending CheckNow response")?;
        Ok(())
    }

    fn handle_get_state(&self, responder: ManagerGetStateResponder) -> Result<(), Error> {
        match self.manager_manager.get_state() {
            State { manager_state, version_available } => {
                responder
                    .send(fidl_fuchsia_update::State {
                        state: Some(manager_state),
                        version_available,
                    })
                    .context("error sending GetState response")?;
                Ok(())
            }
        }
    }

    fn handle_add_monitor(
        &self,
        monitor: fidl::endpoints::ServerEnd<MonitorMarker>,
        _control_handle: ManagerControlHandle,
    ) -> Result<(), Error> {
        let (_stream, handle) =
            // Drop stream b/c Monitor protocol has no client methods
            monitor.into_stream_and_control_handle().context("split AddMonitor ServerEnd")?;
        self.manager_manager.add_permanent_callback(handle);
        Ok(())
    }
}

fn extract_initiator(options: &fidl_fuchsia_update::Options) -> Result<Initiator, Error> {
    if let Some(initiator) = options.initiator {
        match initiator {
            fidl_fuchsia_update::Initiator::User => Ok(Initiator::Manual),
            fidl_fuchsia_update::Initiator::Service => Ok(Initiator::Automatic),
        }
    } else {
        bail!("CheckNow options must specify initiator");
    }
}

impl StateChangeCallback for MonitorControlHandle {
    fn on_state_change(&self, new_state: State) -> Result<(), Error> {
        match new_state {
            State { manager_state, version_available } => {
                self.send_on_state(fidl_fuchsia_update::State {
                    state: Some(manager_state),
                    version_available,
                })
                .context("send_on_state failed for MonitorControlHandle")?;
                Ok(())
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::manager_manager::tests::{
        BlockingUpdateChecker, FakeUpdateApplier, FakeUpdateChecker,
    };
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_update::ManagerState;
    use fidl_fuchsia_update::{
        CheckStartedResult, ManagerMarker, ManagerProxy, MonitorEventStream, MonitorProxy,
    };
    use fuchsia_async as fasync;
    use matches::assert_matches;

    fn state_from_manager_state(manager_state: ManagerState) -> fidl_fuchsia_update::State {
        let state: State = manager_state.into();
        fidl_fuchsia_update::State {
            state: Some(state.manager_state),
            version_available: state.version_available,
        }
    }

    fn options_user() -> fidl_fuchsia_update::Options {
        fidl_fuchsia_update::Options { initiator: Some(fidl_fuchsia_update::Initiator::User) }
    }

    fn spawn_manager_service<C, A>(
        update_checker: C,
        update_applier: A,
    ) -> (ManagerProxy, ManagerService<C, A>)
    where
        C: UpdateChecker,
        A: UpdateApplier,
    {
        let manager_service = ManagerService::<C, A> {
            manager_manager: Arc::new(
                ManagerManager::<C, A, RealStateChangeCallback>::from_checker_and_applier(
                    update_checker,
                    update_applier,
                ),
            ),
        };
        let manager_service_clone = manager_service.clone();
        let (proxy, stream) =
            create_proxy_and_stream::<ManagerMarker>().expect("create_proxy_and_stream");
        fasync::spawn(
            async move { await!(manager_service.handle_request_stream(stream).map(|_| ())) },
        );
        (proxy, manager_service_clone)
    }

    async fn collect_all_on_state_events(monitor: MonitorProxy) -> Vec<fidl_fuchsia_update::State> {
        await!(monitor
            .take_event_stream()
            .map(|r| r.ok().unwrap().into_on_state().unwrap())
            .collect())
    }

    async fn next_n_on_state_events(
        mut event_stream: MonitorEventStream,
        n: usize,
    ) -> (MonitorEventStream, Vec<fidl_fuchsia_update::State>) {
        let mut v = Vec::with_capacity(n);
        for _ in 0..n {
            v.push(await!(event_stream.next()).unwrap().ok().unwrap().into_on_state().unwrap());
        }
        (event_stream, v)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now_monitor_sees_on_state_events() {
        let proxy = spawn_manager_service(
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
        )
        .0;
        let (monitor, monitor_server_end) = fidl::endpoints::create_proxy().expect("create_proxy");

        assert!(await!(proxy.check_now(options_user(), Some(monitor_server_end))).is_ok());

        assert_eq!(
            await!(collect_all_on_state_events(monitor)),
            vec![
                state_from_manager_state(ManagerState::Idle),
                state_from_manager_state(ManagerState::CheckingForUpdates),
                state_from_manager_state(ManagerState::PerformingUpdate),
                state_from_manager_state(ManagerState::EncounteredError),
                state_from_manager_state(ManagerState::Idle),
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_monitor_sees_on_state_events() {
        let proxy = spawn_manager_service(
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
        )
        .0;
        let (monitor, monitor_server_end) = fidl::endpoints::create_proxy().expect("create_proxy");

        assert!(proxy.add_monitor(monitor_server_end).is_ok());
        assert!(await!(proxy.check_now(options_user(), None)).is_ok());

        let events = await!(next_n_on_state_events(monitor.take_event_stream(), 5)).1;
        assert_eq!(
            events,
            vec![
                state_from_manager_state(ManagerState::Idle),
                state_from_manager_state(ManagerState::CheckingForUpdates),
                state_from_manager_state(ManagerState::PerformingUpdate),
                state_from_manager_state(ManagerState::EncounteredError),
                state_from_manager_state(ManagerState::Idle),
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_state() {
        let proxy = spawn_manager_service(
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
        )
        .0;

        assert_eq!(
            await!(proxy.get_state()).expect("get_state"),
            state_from_manager_state(ManagerState::Idle)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_clients_see_on_state_events() {
        let (proxy0, service) = spawn_manager_service(
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
        );

        let (proxy1, stream1) =
            create_proxy_and_stream::<ManagerMarker>().expect("create_proxy_and_stream");
        fasync::spawn(async move { await!(service.handle_request_stream(stream1).map(|_| ())) });

        let (monitor0, monitor_server_end0) =
            fidl::endpoints::create_proxy().expect("create_proxy");
        let (monitor1, monitor_server_end1) =
            fidl::endpoints::create_proxy().expect("create_proxy");

        assert!(proxy0.add_monitor(monitor_server_end0).is_ok());
        assert!(await!(proxy1.check_now(options_user(), Some(monitor_server_end1))).is_ok());

        let events = await!(next_n_on_state_events(monitor0.take_event_stream(), 5)).1;
        assert_eq!(
            events,
            vec![
                state_from_manager_state(ManagerState::Idle),
                state_from_manager_state(ManagerState::CheckingForUpdates),
                state_from_manager_state(ManagerState::PerformingUpdate),
                state_from_manager_state(ManagerState::EncounteredError),
                state_from_manager_state(ManagerState::Idle),
            ]
        );

        assert_eq!(
            await!(collect_all_on_state_events(monitor1)),
            vec![
                state_from_manager_state(ManagerState::Idle),
                state_from_manager_state(ManagerState::CheckingForUpdates),
                state_from_manager_state(ManagerState::PerformingUpdate),
                state_from_manager_state(ManagerState::EncounteredError),
                state_from_manager_state(ManagerState::Idle),
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_attempt_persists_across_client_disconnect_reconnect() {
        let (blocking_update_checker, unblocker) = BlockingUpdateChecker::new_checker_and_sender();
        let fake_update_applier = FakeUpdateApplier::new_error();
        let (proxy0, service) =
            spawn_manager_service(blocking_update_checker, fake_update_applier.clone());

        let (monitor0, monitor_server_end0) =
            fidl::endpoints::create_proxy().expect("create_proxy");
        let (monitor1, monitor_server_end1) =
            fidl::endpoints::create_proxy().expect("create_proxy");

        assert!(await!(proxy0.check_now(options_user(), Some(monitor_server_end0))).is_ok());

        let events = await!(next_n_on_state_events(monitor0.take_event_stream(), 1)).1;
        assert_eq!(events, vec![state_from_manager_state(ManagerState::Idle),]);
        drop(proxy0);
        drop(monitor0);

        let (proxy1, stream1) =
            create_proxy_and_stream::<ManagerMarker>().expect("create_proxy_and_stream");
        fasync::spawn(async move { await!(service.handle_request_stream(stream1).map(|_| ())) });

        assert_matches!(
            await!(proxy1.check_now(options_user(), Some(monitor_server_end1))),
            Ok(CheckStartedResult::InProgress)
        );

        assert_matches!(unblocker.send(()), Ok(()));

        assert_eq!(
            await!(collect_all_on_state_events(monitor1)),
            vec![
                state_from_manager_state(ManagerState::CheckingForUpdates),
                state_from_manager_state(ManagerState::PerformingUpdate),
                state_from_manager_state(ManagerState::EncounteredError),
                state_from_manager_state(ManagerState::Idle),
            ]
        );

        assert_eq!(fake_update_applier.call_count(), 1);
    }
}
