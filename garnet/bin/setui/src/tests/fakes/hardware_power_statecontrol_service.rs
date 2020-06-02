// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::tests::fakes::base::Service;
use anyhow::{format_err, Error};
use fidl::endpoints::{ServerEnd, ServiceMarker};
use fidl_fuchsia_hardware_power_statecontrol::{AdminRequest, RebootReason};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::TryStreamExt;
use parking_lot::RwLock;
use std::{collections::VecDeque, sync::Arc};

#[derive(PartialEq, Debug, Eq, Hash, Clone, Copy)]
pub enum Action {
    Reboot,
}

#[derive(PartialEq, Debug, Eq, Hash, Clone, Copy)]
pub enum Response {
    Fail,
}

impl Response {
    fn to_zx_status_result(self) -> Result<(), i32> {
        match self {
            Response::Fail => Err(-1),
        }
    }
}

/// An implementation of hardware power statecontrol services that records the
/// actions invoked on it.
pub struct HardwarePowerStatecontrolService {
    recorded_actions: Arc<RwLock<Vec<Action>>>,
    planned_actions: Arc<RwLock<VecDeque<(Action, Response)>>>,
}

impl HardwarePowerStatecontrolService {
    pub fn new() -> Self {
        Self {
            recorded_actions: Arc::new(RwLock::new(Vec::new())),
            planned_actions: Arc::new(RwLock::new(VecDeque::new())),
        }
    }

    pub fn verify_action_sequence(&self, actions: Vec<Action>) -> bool {
        let recorded_actions = self.recorded_actions.read();
        actions.len() == recorded_actions.len()
            && recorded_actions
                .iter()
                .zip(actions.iter())
                .all(|(action1, action2)| action1 == action2)
    }

    pub fn plan_action_response(&self, action: Action, response: Response) {
        let mut planned_actions = self.planned_actions.write();
        planned_actions.push_front((action, response));
    }
}

impl Service for HardwarePowerStatecontrolService {
    fn can_handle_service(&self, service_name: &str) -> bool {
        return service_name == fidl_fuchsia_hardware_power_statecontrol::AdminMarker::NAME;
    }

    fn process_stream(&self, service_name: &str, channel: zx::Channel) -> Result<(), Error> {
        if !self.can_handle_service(service_name) {
            return Err(format_err!("unsupported"));
        }

        let mut manager_stream =
            ServerEnd::<fidl_fuchsia_hardware_power_statecontrol::AdminMarker>::new(channel)
                .into_stream()?;

        let planned_actions = self.planned_actions.clone();
        let recorded_actions_clone = self.recorded_actions.clone();
        fasync::spawn(async move {
            while let Some(req) = manager_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    AdminRequest::Reboot { reason: RebootReason::UserRequest, responder } => {
                        let mut response = {
                            let mut planned_actions = planned_actions.write();
                            if let Some((Action::Reboot, _)) = planned_actions.back() {
                                let (_, response) = planned_actions.pop_back().unwrap();
                                response.to_zx_status_result()
                            } else {
                                Ok(())
                            }
                        };

                        if let Ok(_) = response {
                            recorded_actions_clone.write().push(Action::Reboot);
                        }

                        responder.send(&mut response).unwrap();
                    }
                    _ => {}
                }
            }
        });

        Ok(())
    }
}
