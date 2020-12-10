// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::base::{AgentError, Context, Invocation, InvocationResult, Lifespan};
/// The Restore Agent is responsible for signaling to all components to restore
/// external sources to the last known value. It is invoked during startup.
use crate::blueprint_definition;
use crate::internal::agent::Payload;
use crate::internal::event::{restore, Event, Publisher};
use crate::internal::switchboard;
use crate::message::base::{Audience, MessageEvent};
use crate::switchboard::base::{SettingRequest, SettingType, SwitchboardError};
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use futures::StreamExt;
use std::collections::HashSet;

blueprint_definition!("restore_agent", crate::agent::restore_agent::RestoreAgent::create);

#[derive(Debug)]
pub struct RestoreAgent {
    switchboard_messenger: switchboard::message::Messenger,
    event_publisher: Publisher,
    available_components: HashSet<SettingType>,
}

impl RestoreAgent {
    async fn create(mut context: Context) {
        let messenger = if let Ok(messenger) = context.create_switchboard_messenger().await {
            messenger
        } else {
            context.get_publisher().send_event(Event::Custom(
                "Could not acquire switchboard messenger in RestoreAgent",
            ));
            return;
        };

        let mut agent = RestoreAgent {
            switchboard_messenger: messenger,
            event_publisher: context.get_publisher(),
            available_components: context.available_components.clone(),
        };

        fasync::Task::spawn(async move {
            while let Some(event) = context.receptor.next().await {
                if let MessageEvent::Message(Payload::Invocation(invocation), client) = event {
                    client.reply(Payload::Complete(agent.handle(invocation).await)).send().ack();
                }
            }
        })
        .detach();
    }

    async fn handle(&mut self, invocation: Invocation) -> InvocationResult {
        match invocation.lifespan {
            Lifespan::Initialization => {
                for component in self.available_components.clone() {
                    let mut receptor = self
                        .switchboard_messenger
                        .message(
                            switchboard::Payload::Action(switchboard::Action::Request(
                                component,
                                SettingRequest::Restore,
                            )),
                            Audience::Address(switchboard::Address::Switchboard),
                        )
                        .send();

                    if let switchboard::Payload::Action(switchboard::Action::Response(response)) =
                        receptor.next_payload().await.map_err(|_| AgentError::UnexpectedError)?.0
                    {
                        match response {
                            Ok(_) => {
                                continue;
                            }
                            Err(SwitchboardError::UnimplementedRequest(setting_type, _)) => {
                                self.event_publisher
                                    .send_event(Event::Restore(restore::Event::NoOp(setting_type)));
                                continue;
                            }
                            Err(SwitchboardError::UnhandledType(setting_type)) => {
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
