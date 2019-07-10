// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::apply::Initiator;
use crate::manager_manager::{
    ManagerManager, RealUpdateApplier, RealUpdateChecker, StateChangeCallback, UpdateApplier,
    UpdateChecker,
};
use failure::{err_msg, Error, ResultExt};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_update::{
    ManagerCheckNowResponder, ManagerControlHandle, ManagerGetStateResponder, ManagerRequest,
    ManagerRequestStream, ManagerState, MonitorControlHandle, MonitorMarker,
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
        responder
            .send(fidl_fuchsia_update::State {
                state: Some(self.manager_manager.get_state()),
                version_available: None,
            })
            .context("error sending GetState response")?;
        Ok(())
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
        Err(err_msg("CheckNow options must specify initiator"))?
    }
}

impl StateChangeCallback for MonitorControlHandle {
    fn on_state_change(&self, new_state: ManagerState) -> Result<(), Error> {
        self.send_on_state(fidl_fuchsia_update::State {
            state: Some(new_state),
            version_available: None,
        })
        .context("send_on_state failed for MonitorControlHandle")?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::check::SystemUpdateStatus;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_update::{CheckStartedResult, ManagerState};
    use fidl_fuchsia_update::{
        ManagerMarker, ManagerProxy, MonitorEvent, MonitorEventStream, MonitorProxy,
    };
    use fuchsia_async as fasync;
    use fuchsia_merkle::Hash;
    use futures::channel::oneshot;
    use futures::future::BoxFuture;
    use matches::assert_matches;
    use std::sync::atomic::AtomicBool;

    const CURRENT_SYSTEM_IMAGE: [u8; 32] = [0u8; 32];
    const LATEST_SYSTEM_IMAGE: [u8; 32] = [1u8; 32];

    #[derive(Clone)]
    struct FakeUpdateChecker {
        result: Result<SystemUpdateStatus, crate::errors::ErrorKind>,
    }
    impl FakeUpdateChecker {
        fn new_up_to_date() -> Self {
            Self {
                result: Ok(SystemUpdateStatus::UpToDate {
                    system_image: CURRENT_SYSTEM_IMAGE.into(),
                }),
            }
        }
        fn new_update_available() -> Self {
            Self {
                result: Ok(SystemUpdateStatus::UpdateAvailable {
                    current_system_image: CURRENT_SYSTEM_IMAGE.into(),
                    latest_system_image: LATEST_SYSTEM_IMAGE.into(),
                }),
            }
        }
        fn new_error() -> Self {
            Self { result: Err(crate::errors::ErrorKind::ResolveUpdatePackage) }
        }
    }
    impl UpdateChecker for FakeUpdateChecker {
        fn check(&self) -> BoxFuture<Result<SystemUpdateStatus, crate::errors::Error>> {
            future::ready(self.result.clone().map_err(|e| e.into())).boxed()
        }
    }

    #[derive(Clone)]
    struct FakeUpdateApplier {
        result: Result<(), crate::errors::ErrorKind>,
        was_called: Arc<AtomicBool>,
    }
    impl FakeUpdateApplier {
        fn new_success() -> Self {
            Self { result: Ok(()), was_called: Arc::new(AtomicBool::new(false)) }
        }
        fn new_error() -> Self {
            Self {
                result: Err(crate::errors::ErrorKind::SystemUpdaterFailed),
                was_called: Arc::new(AtomicBool::new(false)),
            }
        }
    }
    impl UpdateApplier for FakeUpdateApplier {
        fn apply(
            &self,
            _current_system_image: Hash,
            _latest_system_image: Hash,
            _initiator: Initiator,
        ) -> BoxFuture<Result<(), crate::errors::Error>> {
            self.was_called.store(true, std::sync::atomic::Ordering::Release);
            future::ready(self.result.clone().map_err(|e| e.into())).boxed()
        }
    }

    fn state_from_manager_state(manager_state: ManagerState) -> fidl_fuchsia_update::State {
        fidl_fuchsia_update::State { state: Some(manager_state), version_available: None }
    }

    fn options_user() -> fidl_fuchsia_update::Options {
        fidl_fuchsia_update::Options { initiator: Some(fidl_fuchsia_update::Initiator::User) }
    }

    fn spawn_manager_service<C, A>(update_checker: C, update_applier: A) -> ManagerProxy
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
        let (proxy, stream) =
            create_proxy_and_stream::<ManagerMarker>().expect("create_proxy_and_stream");
        fasync::spawn(
            async move { await!(manager_service.handle_request_stream(stream).map(|_| ())) },
        );
        proxy
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

    async fn wait_until_state(monitor: MonitorProxy, manager_state: ManagerState) {
        let state = state_from_manager_state(manager_state);
        let mut stream = monitor.take_event_stream();
        loop {
            match await!(stream.next()) {
                Some(res) => match res {
                    Ok(MonitorEvent::OnState { state: new_state }) => {
                        if new_state == state {
                            return;
                        }
                    }
                    Err(e) => panic!("wait_until_state failed {}", e),
                },
                None => panic!(
                    "wait_until_state emptied stream without finding state {:?}",
                    manager_state
                ),
            }
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_state_returns_idle() {
        let proxy = spawn_manager_service(
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
        );

        let res = await!(proxy.get_state());

        assert_matches!(res, Ok(ref state) if state == &state_from_manager_state(ManagerState::Idle));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now_returns_started() {
        let proxy = spawn_manager_service(
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
        );

        let res = await!(proxy.check_now(options_user(), None));

        assert_matches!(res, Ok(ref state) if state == &CheckStartedResult::Started);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now_temporary_handles_dropped_after_check() {
        let proxy = spawn_manager_service(
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
        );
        let (monitor, monitor_server_end) = fidl::endpoints::create_proxy().expect("create_proxy");

        assert!(await!(proxy.check_now(options_user(), Some(monitor_server_end))).is_ok());

        // collect verifies that the temporary control handles are dropped
        // after the check, otherwise the test would hang
        await!(collect_all_on_state_events(monitor));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now_up_to_date_on_state_events() {
        let proxy = spawn_manager_service(
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
        );
        let (monitor, monitor_server_end) = fidl::endpoints::create_proxy().expect("create_proxy");

        assert!(await!(proxy.check_now(options_user(), Some(monitor_server_end))).is_ok());

        assert_eq!(
            await!(collect_all_on_state_events(monitor)),
            vec![
                state_from_manager_state(ManagerState::CheckingForUpdates),
                state_from_manager_state(ManagerState::Idle)
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now_update_available_on_state_events() {
        let proxy = spawn_manager_service(
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
        );
        let (monitor, monitor_server_end) = fidl::endpoints::create_proxy().expect("create_proxy");

        assert!(await!(proxy.check_now(options_user(), Some(monitor_server_end))).is_ok());

        assert_eq!(
            await!(collect_all_on_state_events(monitor)),
            vec![
                state_from_manager_state(ManagerState::CheckingForUpdates),
                state_from_manager_state(ManagerState::PerformingUpdate),
                state_from_manager_state(ManagerState::EncounteredError),
                state_from_manager_state(ManagerState::Idle)
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_monitor_sees_check_now_events() {
        let proxy = spawn_manager_service(
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_success(),
        );
        let (monitor, monitor_server_end) = fidl::endpoints::create_proxy().expect("create_proxy");

        proxy.add_monitor(monitor_server_end).expect("add_monitor");
        assert!(await!(proxy.check_now(options_user(), None)).is_ok());

        assert_eq!(
            await!(next_n_on_state_events(monitor.take_event_stream(), 3)).1,
            vec![
                state_from_manager_state(ManagerState::CheckingForUpdates),
                state_from_manager_state(ManagerState::PerformingUpdate),
                state_from_manager_state(ManagerState::WaitingForReboot),
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_monitor_sees_consecutive_check_now_on_state_events() {
        let proxy = spawn_manager_service(
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
        );
        let (monitor, monitor_server_end) = fidl::endpoints::create_proxy().expect("create_proxy");

        proxy.add_monitor(monitor_server_end).expect("add_monitor");
        assert!(await!(proxy.check_now(options_user(), None)).is_ok());

        let event_stream = monitor.take_event_stream();
        let (event_stream, events) = await!(next_n_on_state_events(event_stream, 4));
        assert_eq!(
            events,
            vec![
                state_from_manager_state(ManagerState::CheckingForUpdates),
                state_from_manager_state(ManagerState::PerformingUpdate),
                state_from_manager_state(ManagerState::EncounteredError),
                state_from_manager_state(ManagerState::Idle),
            ]
        );

        // Perform 2nd check_now after monitor events extracted to guarantee that 1st check_now
        // has completed
        assert!(await!(proxy.check_now(options_user(), None)).is_ok());

        assert_eq!(
            await!(next_n_on_state_events(event_stream, 4)).1,
            vec![
                state_from_manager_state(ManagerState::CheckingForUpdates),
                state_from_manager_state(ManagerState::PerformingUpdate),
                state_from_manager_state(ManagerState::EncounteredError),
                state_from_manager_state(ManagerState::Idle),
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_applier_called_if_update_available() {
        let update_applier = FakeUpdateApplier::new_success();
        let proxy = spawn_manager_service(
            FakeUpdateChecker::new_update_available(),
            update_applier.clone(),
        );
        let (monitor, monitor_server_end) = fidl::endpoints::create_proxy().expect("create_proxy");

        proxy.add_monitor(monitor_server_end).expect("add_monitor");
        assert!(await!(proxy.check_now(options_user(), None)).is_ok());

        await!(wait_until_state(monitor, ManagerState::WaitingForReboot));
        assert!(update_applier.was_called.load(std::sync::atomic::Ordering::Acquire));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_applier_not_called_if_up_to_date() {
        let update_applier = FakeUpdateApplier::new_success();
        let proxy =
            spawn_manager_service(FakeUpdateChecker::new_up_to_date(), update_applier.clone());
        let (monitor, monitor_server_end) = fidl::endpoints::create_proxy().expect("create_proxy");

        proxy.add_monitor(monitor_server_end).expect("add_monitor");
        assert!(await!(proxy.check_now(options_user(), None)).is_ok());

        await!(wait_until_state(monitor, ManagerState::Idle));
        assert!(!update_applier.was_called.load(std::sync::atomic::Ordering::Acquire));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_return_to_idle_on_update_check_error() {
        let proxy =
            spawn_manager_service(FakeUpdateChecker::new_error(), FakeUpdateApplier::new_success());
        let (monitor, monitor_server_end) = fidl::endpoints::create_proxy().expect("create_proxy");

        assert!(await!(proxy.check_now(options_user(), Some(monitor_server_end))).is_ok());

        await!(wait_until_state(monitor, ManagerState::Idle));
        assert_matches!(
            await!(proxy.get_state()),
            Ok(ref state) if state == &state_from_manager_state(ManagerState::Idle)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_return_to_idle_on_update_apply_error() {
        let proxy = spawn_manager_service(
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
        );
        let (monitor, monitor_server_end) = fidl::endpoints::create_proxy().expect("create_proxy");

        assert!(await!(proxy.check_now(options_user(), Some(monitor_server_end))).is_ok());

        await!(wait_until_state(monitor, ManagerState::Idle));
        assert_matches!(
            await!(proxy.get_state()),
            Ok(ref state) if state == &state_from_manager_state(ManagerState::Idle)
        );
    }

    #[derive(Clone)]
    struct BlockingUpdateChecker {
        blocker: future::Shared<oneshot::Receiver<()>>,
    }
    impl BlockingUpdateChecker {
        fn new_checker_and_sender() -> (Self, oneshot::Sender<()>) {
            let (sender, receiver) = oneshot::channel();
            let blocking_update_checker = BlockingUpdateChecker { blocker: receiver.shared() };
            (blocking_update_checker, sender)
        }
    }
    impl UpdateChecker for BlockingUpdateChecker {
        fn check(&self) -> BoxFuture<Result<SystemUpdateStatus, crate::errors::Error>> {
            let blocker = self.blocker.clone();
            async move {
                assert!(await!(blocker).is_ok(), "blocking future cancelled");
                Ok(SystemUpdateStatus::UpdateAvailable {
                    current_system_image: CURRENT_SYSTEM_IMAGE.into(),
                    latest_system_image: LATEST_SYSTEM_IMAGE.into(),
                })
            }
                .boxed()
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_state_in_checking_for_updates() {
        let (blocking_update_checker, _sender) = BlockingUpdateChecker::new_checker_and_sender();
        let proxy = spawn_manager_service(blocking_update_checker, FakeUpdateApplier::new_error());

        assert!(await!(proxy.check_now(options_user(), None)).is_ok());

        assert_matches!(
            await!(proxy.get_state()),
            Ok(ref state) if state == &state_from_manager_state(ManagerState::CheckingForUpdates)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_only_one_check_at_a_time() {
        let (blocking_update_checker, sender) = BlockingUpdateChecker::new_checker_and_sender();
        let proxy = spawn_manager_service(blocking_update_checker, FakeUpdateApplier::new_error());
        let (monitor0, monitor_server_end0) =
            fidl::endpoints::create_proxy().expect("create_proxy");
        let (monitor1, monitor_server_end1) =
            fidl::endpoints::create_proxy().expect("create_proxy");

        let res0 = await!(proxy.check_now(options_user(), Some(monitor_server_end0)));
        // handle_check_now locks state before responding, so awaiting on the first response guarantees
        // that the service is in CheckingForUpdates state
        let res1 = await!(proxy.check_now(options_user(), Some(monitor_server_end1)));
        assert!(sender.send(()).is_ok());

        assert_matches!(res0, Ok(ref state) if state == &CheckStartedResult::Started);
        assert_eq!(
            await!(collect_all_on_state_events(monitor0)),
            vec![
                state_from_manager_state(ManagerState::CheckingForUpdates),
                state_from_manager_state(ManagerState::PerformingUpdate),
                state_from_manager_state(ManagerState::EncounteredError),
                state_from_manager_state(ManagerState::Idle)
            ]
        );

        assert_matches!(res1, Ok(ref state) if state == &CheckStartedResult::InProgress);
        assert_eq!(
            await!(collect_all_on_state_events(monitor1)),
            vec![
                state_from_manager_state(ManagerState::PerformingUpdate),
                state_from_manager_state(ManagerState::EncounteredError),
                state_from_manager_state(ManagerState::Idle)
            ]
        );
    }
}
