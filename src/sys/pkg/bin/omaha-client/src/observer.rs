// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    fidl::FidlServer,
    inspect::{LastResultsNode, ProtocolStateNode, ScheduleNode},
};
use futures::{future::LocalBoxFuture, prelude::*};
use omaha_client::{
    clock,
    common::{ProtocolState, UpdateCheckSchedule},
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
    ) -> Self {
        FuchsiaObserver {
            fidl_server,
            schedule_node,
            protocol_state_node,
            last_results_node,
            last_update_start_time: SystemTime::UNIX_EPOCH,
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
        future::ready(()).boxed_local()
    }
}
