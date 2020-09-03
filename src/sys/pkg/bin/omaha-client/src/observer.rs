// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    cobalt::notify_cobalt_current_software_distribution,
    fidl::{FidlServer, StateMachineController},
    inspect::{LastResultsNode, ProtocolStateNode, ScheduleNode},
};
use fuchsia_inspect::Node;
use omaha_client::{
    clock,
    common::{AppSet, ProtocolState, UpdateCheckSchedule},
    protocol::response::Response,
    state_machine::{update_check, InstallProgress, State, StateMachineEvent, UpdateCheckError},
    storage::Storage,
};
use std::cell::RefCell;
use std::rc::Rc;
use std::time::SystemTime;

mod platform;

pub struct FuchsiaObserver<ST, SM>
where
    ST: Storage,
    SM: StateMachineController,
{
    fidl_server: Rc<RefCell<FidlServer<ST, SM>>>,
    schedule_node: ScheduleNode,
    protocol_state_node: ProtocolStateNode,
    last_results_node: LastResultsNode,
    last_update_start_time: SystemTime,
    app_set: AppSet,
    notified_cobalt: bool,
    target_version: Option<String>,
    platform_metrics_emitter: platform::Emitter,
}

impl<ST, SM> FuchsiaObserver<ST, SM>
where
    ST: Storage + 'static,
    SM: StateMachineController,
{
    pub fn new(
        fidl_server: Rc<RefCell<FidlServer<ST, SM>>>,
        schedule_node: ScheduleNode,
        protocol_state_node: ProtocolStateNode,
        last_results_node: LastResultsNode,
        app_set: AppSet,
        notified_cobalt: bool,
        platform_metrics_node: Node,
    ) -> Self {
        FuchsiaObserver {
            fidl_server,
            schedule_node,
            protocol_state_node,
            last_results_node,
            last_update_start_time: SystemTime::UNIX_EPOCH,
            app_set,
            notified_cobalt,
            target_version: None,
            platform_metrics_emitter: platform::Emitter::from_node(platform_metrics_node),
        }
    }

    pub async fn on_event(&mut self, event: StateMachineEvent) {
        match event {
            StateMachineEvent::StateChange(state) => self.on_state_change(state).await,
            StateMachineEvent::ScheduleChange(schedule) => self.on_schedule_change(&schedule),
            StateMachineEvent::ProtocolStateChange(state) => self.on_protocol_state_change(&state),
            StateMachineEvent::UpdateCheckResult(result) => {
                self.on_update_check_result(&result).await
            }
            StateMachineEvent::InstallProgressChange(progress) => {
                self.on_progress_change(progress).await
            }
            StateMachineEvent::OmahaServerResponse(response) => self.on_omaha_response(response),
        }
    }

    async fn on_state_change(&mut self, state: State) {
        match state {
            State::Idle => {
                self.target_version = None;
            }
            State::CheckingForUpdates => {
                self.last_update_start_time = clock::now();
                self.platform_metrics_emitter.emit(platform::Event::CheckingForUpdates);
            }
            State::ErrorCheckingForUpdate => {
                self.platform_metrics_emitter.emit(platform::Event::ErrorCheckingForUpdate);
            }
            State::NoUpdateAvailable => {
                self.platform_metrics_emitter.emit(platform::Event::NoUpdateAvailable);
            }
            State::InstallingUpdate => {
                self.platform_metrics_emitter.emit(platform::Event::InstallingUpdate {
                    target_version: self.target_version.as_deref(),
                });
            }
            State::InstallationDeferredByPolicy => {
                self.platform_metrics_emitter.emit(platform::Event::InstallationDeferredByPolicy {
                    target_version: self.target_version.as_deref(),
                });
            }
            State::InstallationError => {
                self.platform_metrics_emitter.emit(platform::Event::InstallationError {
                    target_version: self.target_version.as_deref(),
                });
            }
            State::WaitingForReboot => {
                self.platform_metrics_emitter.emit(platform::Event::WaitingForReboot {
                    target_version: self.target_version.as_deref(),
                });
            }
        }
        FidlServer::on_state_change(Rc::clone(&self.fidl_server), state).await
    }

    fn on_schedule_change(&mut self, schedule: &UpdateCheckSchedule) {
        self.schedule_node.set(schedule);
    }

    fn on_protocol_state_change(&mut self, protocol_state: &ProtocolState) {
        self.protocol_state_node.set(protocol_state);
    }

    async fn on_update_check_result(
        &mut self,
        result: &Result<update_check::Response, UpdateCheckError>,
    ) {
        self.last_results_node.add_result(self.last_update_start_time, result);

        // TODO(senj): Remove once channel is in vbmeta.
        let no_update = result
            .as_ref()
            .map(|response| {
                response
                    .app_responses
                    .iter()
                    .all(|app_response| app_response.result == update_check::Action::NoUpdate)
            })
            .unwrap_or(false);

        if !self.notified_cobalt && no_update {
            notify_cobalt_current_software_distribution(self.app_set.clone()).await;
            self.notified_cobalt = true;
        }
    }

    async fn on_progress_change(&mut self, progress: InstallProgress) {
        FidlServer::on_progress_change(Rc::clone(&self.fidl_server), progress).await
    }

    fn on_omaha_response(&mut self, response: Response) {
        self.target_version = response
            .apps
            .into_iter()
            .nth(0)
            .and_then(|app| app.update_check)
            .and_then(|update_check| update_check.manifest)
            .map(|manifest| manifest.version);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fidl::{FidlServerBuilder, MockOrRealStateMachineController};
    use anyhow::anyhow;
    use fuchsia_async as fasync;
    use fuchsia_inspect::Inspector;
    use omaha_client::{
        common::{App, UserCounting},
        protocol::{
            response::{self, Manifest, UpdateCheck},
            Cohort,
        },
        storage::MemStorage,
    };

    async fn new_test_observer() -> FuchsiaObserver<MemStorage, MockOrRealStateMachineController> {
        let fidl = FidlServerBuilder::new().build().await;
        let inspector = Inspector::new();
        let schedule_node = ScheduleNode::new(inspector.root().create_child("schedule"));
        let protocol_state_node =
            ProtocolStateNode::new(inspector.root().create_child("protocol_state"));
        let last_results_node = LastResultsNode::new(inspector.root().create_child("last_results"));
        let platform_metrics_node = inspector.root().create_child("platform_metrics");
        let app_set = AppSet::new(vec![App::builder("id", [1, 2]).build()]);
        FuchsiaObserver::new(
            fidl,
            schedule_node,
            protocol_state_node,
            last_results_node,
            app_set,
            false,
            platform_metrics_node,
        )
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_notify_cobalt() {
        let mut observer = new_test_observer().await;

        assert!(!observer.notified_cobalt);
        observer
            .on_update_check_result(&Err(UpdateCheckError::InstallPlan(anyhow!("some error"))))
            .await;
        assert!(!observer.notified_cobalt);

        let app_response = update_check::AppResponse {
            app_id: "".to_string(),
            cohort: Cohort::default(),
            user_counting: UserCounting::ClientRegulatedByDate(None),
            result: update_check::Action::NoUpdate,
        };
        let result = Ok(update_check::Response {
            app_responses: vec![app_response],
            server_dictated_poll_interval: None,
        });
        observer.on_update_check_result(&result).await;
        assert!(observer.notified_cobalt);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cache_target_version() {
        let mut observer = new_test_observer().await;
        assert_eq!(observer.target_version, None);
        let response = Response {
            apps: vec![response::App {
                update_check: Some(UpdateCheck {
                    manifest: Some(Manifest {
                        version: "3.2.1".to_string(),
                        ..Manifest::default()
                    }),
                    ..UpdateCheck::default()
                }),
                ..response::App::default()
            }],
            ..Response::default()
        };
        observer.on_omaha_response(response);
        assert_eq!(observer.target_version, Some("3.2.1".to_string()));
        observer.on_state_change(State::Idle).await;
        assert_eq!(observer.target_version, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cache_target_version_no_update() {
        let mut observer = new_test_observer().await;
        assert_eq!(observer.target_version, None);
        let response = Response {
            apps: vec![response::App {
                update_check: Some(UpdateCheck::no_update()),
                ..response::App::default()
            }],
            ..Response::default()
        };
        observer.on_omaha_response(response);
        assert_eq!(observer.target_version, None);
    }
}
