// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    fidl::FidlServer,
    inspect::{ProtocolStateNode, ScheduleNode},
};
use futures::{future::LocalBoxFuture, prelude::*};
use omaha_client::{
    common::{ProtocolState, UpdateCheckSchedule},
    http_request::HttpRequest,
    installer::Installer,
    metrics::MetricsReporter,
    policy::PolicyEngine,
    state_machine::{Observer, State, Timer},
    storage::Storage,
};
use std::cell::RefCell;
use std::rc::Rc;

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
    ) -> Self {
        FuchsiaObserver { fidl_server, schedule_node, protocol_state_node }
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
}
