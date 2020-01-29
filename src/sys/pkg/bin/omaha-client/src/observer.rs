// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    cobalt::notify_cobalt_current_channel,
    fidl::FidlServer,
    inspect::{LastResultsNode, ProtocolStateNode, ScheduleNode},
};
use futures::{future::LocalBoxFuture, prelude::*};
use omaha_client::{
    clock,
    common::{AppSet, ProtocolState, UpdateCheckSchedule},
    http_request::HttpRequest,
    installer::Installer,
    metrics::MetricsReporter,
    policy::PolicyEngine,
    state_machine::{update_check, Observer, State, Timer, UpdateCheckError},
    storage::Storage,
};
use std::cell::RefCell;
use std::rc::Rc;
use std::time::SystemTime;

pub struct FuchsiaObserver<PE, HR, IN, TM, MR, ST>
where
    PE: PolicyEngine,
    HR: HttpRequest,
    IN: Installer,
    TM: Timer,
    MR: MetricsReporter,
    ST: Storage,
{
    fidl_server: Rc<RefCell<FidlServer<PE, HR, IN, TM, MR, ST>>>,
    schedule_node: ScheduleNode,
    protocol_state_node: ProtocolStateNode,
    last_results_node: LastResultsNode,
    last_update_start_time: SystemTime,
    app_set: AppSet,
    notified_cobalt: bool,
}

impl<PE, HR, IN, TM, MR, ST> FuchsiaObserver<PE, HR, IN, TM, MR, ST>
where
    PE: PolicyEngine + 'static,
    HR: HttpRequest + 'static,
    IN: Installer + 'static,
    TM: Timer + 'static,
    MR: MetricsReporter + 'static,
    ST: Storage + 'static,
{
    pub fn new(
        fidl_server: Rc<RefCell<FidlServer<PE, HR, IN, TM, MR, ST>>>,
        schedule_node: ScheduleNode,
        protocol_state_node: ProtocolStateNode,
        last_results_node: LastResultsNode,
        app_set: AppSet,
        notified_cobalt: bool,
    ) -> Self {
        FuchsiaObserver {
            fidl_server,
            schedule_node,
            protocol_state_node,
            last_results_node,
            last_update_start_time: SystemTime::UNIX_EPOCH,
            app_set,
            notified_cobalt,
        }
    }
}

impl<PE, HR, IN, TM, MR, ST> Observer for FuchsiaObserver<PE, HR, IN, TM, MR, ST>
where
    PE: PolicyEngine + 'static,
    HR: HttpRequest + 'static,
    IN: Installer + 'static,
    TM: Timer + 'static,
    MR: MetricsReporter + 'static,
    ST: Storage + 'static,
{
    fn on_state_change(&mut self, state: State) -> LocalBoxFuture<'_, ()> {
        if state == State::CheckingForUpdates {
            self.last_update_start_time = clock::now();
        }
        async move {
            let mut server = self.fidl_server.borrow_mut();
            server.on_state_change(state).await;
        }
        .boxed_local()
    }

    fn on_schedule_change(&mut self, schedule: &UpdateCheckSchedule) -> LocalBoxFuture<'_, ()> {
        self.schedule_node.set(schedule);
        future::ready(()).boxed_local()
    }

    fn on_protocol_state_change(
        &mut self,
        protocol_state: &ProtocolState,
    ) -> LocalBoxFuture<'_, ()> {
        self.protocol_state_node.set(protocol_state);
        future::ready(()).boxed_local()
    }

    fn on_update_check_result(
        &mut self,
        result: &Result<update_check::Response, UpdateCheckError>,
    ) -> LocalBoxFuture<'_, ()> {
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
        async move {
            if !self.notified_cobalt && no_update {
                notify_cobalt_current_channel(self.app_set.clone()).await;
                self.notified_cobalt = true;
            }
        }
        .boxed_local()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fidl::FidlServerBuilder;
    use fuchsia_async as fasync;
    use fuchsia_inspect::Inspector;
    use omaha_client::{
        common::{App, UserCounting},
        policy::CheckDecision,
        protocol::Cohort,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_notify_cobalt() {
        let fidl = Rc::new(RefCell::new(FidlServerBuilder::new().build().await));
        let inspector = Inspector::new();
        let schedule_node = ScheduleNode::new(inspector.root().create_child("schedule"));
        let protocol_state_node =
            ProtocolStateNode::new(inspector.root().create_child("protocol_state"));
        let last_results_node = LastResultsNode::new(inspector.root().create_child("last_results"));
        let app_set = AppSet::new(vec![App::new("id", [1, 2], Cohort::default())]);
        let mut observer = FuchsiaObserver::new(
            fidl,
            schedule_node,
            protocol_state_node,
            last_results_node,
            app_set,
            false,
        );

        assert!(!observer.notified_cobalt);
        observer
            .on_update_check_result(&Err(UpdateCheckError::Policy(CheckDecision::DeniedByPolicy)))
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
}
