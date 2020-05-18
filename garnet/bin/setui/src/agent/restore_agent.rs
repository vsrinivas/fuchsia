// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The Restore Agent is responsible for signaling to all components to restore
/// external sources to the last known value. It is invoked during startup.
use crate::agent::base::{AgentError, Context, Invocation, InvocationResult, Lifespan};
use crate::internal::agent::Payload;
use crate::message::base::MessageEvent;
use crate::switchboard::base::{SettingRequest, SwitchboardError};
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_info};

#[derive(Debug)]
pub struct RestoreAgent;

impl RestoreAgent {
    pub fn new() -> RestoreAgent {
        RestoreAgent {}
    }

    pub fn create(mut context: Context) {
        let mut agent = RestoreAgent::new();

        fasync::spawn(async move {
            while let Ok(event) = context.receptor.watch().await {
                if let MessageEvent::Message(Payload::Invocation(invocation), client) = event {
                    client.reply(Payload::Complete(agent.handle(invocation).await)).send().ack();
                }
            }
        });
    }

    async fn handle(&mut self, invocation: Invocation) -> InvocationResult {
        match invocation.lifespan.clone() {
            Lifespan::Initialization(context) => {
                for component in context.available_components {
                    if let Ok(result_rx) =
                        context.switchboard_client.request(component, SettingRequest::Restore).await
                    {
                        match result_rx.await {
                            Ok(Ok(_)) => {
                                continue;
                            }
                            Ok(Err(SwitchboardError::UnimplementedRequest {
                                setting_type,
                                request: _,
                            })) => {
                                fx_log_info!("setting does not support restore:{:?}", setting_type);
                                continue;
                            }
                            Ok(Err(SwitchboardError::UnhandledType { setting_type })) => {
                                fx_log_info!(
                                    "setting not available for restore: {:?}",
                                    setting_type
                                );
                                continue;
                            }
                            _ => {
                                fx_log_err!("error during restore for {:?}", component);
                                return Err(AgentError::UnexpectedError);
                            }
                        }
                    } else {
                        return Err(AgentError::UnexpectedError);
                    }
                }
            }
            _ => {
                return Err(AgentError::UnhandledLifespan);
            }
        }

        Ok(())
    }
}
