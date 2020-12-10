// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::base::{AgentError, Authority, BlueprintHandle, Context, Invocation, Lifespan};

use crate::internal::agent;
use crate::internal::event;
use crate::internal::switchboard;
use crate::message::base::{Audience, MessengerType};
use crate::monitor;
use crate::service_context::ServiceContextHandle;
use crate::switchboard::base::SettingType;
use anyhow::{format_err, Error};
use async_trait::async_trait;
use std::collections::HashSet;

/// AuthorityImpl is the default implementation of the Authority trait. It
/// provides the ability to execute agents sequentially or simultaneously for a
/// given stage.
pub struct AuthorityImpl {
    // A mapping of agent addresses
    agent_signatures: Vec<agent::message::Signature>,
    // Factory to generate messengers to comunicate with the agent
    messenger_factory: agent::message::Factory,
    // Factory passed to agents for communicating with the switchboard.
    switchboard_messenger_factory: switchboard::message::Factory,
    // Messenger
    messenger: agent::message::Messenger,
    // Factory to generate event messengers
    event_factory: event::message::Factory,
    // Available components
    available_components: HashSet<SettingType>,
    // Available resource monitors
    resource_monitor_actor: Option<monitor::environment::Actor>,
}

impl AuthorityImpl {
    pub async fn create(
        messenger_factory: agent::message::Factory,
        switchboard_messenger_factory: switchboard::message::Factory,
        event_factory: event::message::Factory,
        available_components: HashSet<SettingType>,
        resource_monitor_actor: Option<monitor::environment::Actor>,
    ) -> Result<AuthorityImpl, Error> {
        let messenger_result = messenger_factory.create(MessengerType::Unbound).await;

        if messenger_result.is_err() {
            return Err(anyhow::format_err!("could not create agent messenger for authority"));
        }

        let (client, _) = messenger_result.unwrap();
        return Ok(AuthorityImpl {
            agent_signatures: Vec::new(),
            messenger_factory,
            switchboard_messenger_factory,
            messenger: client,
            event_factory,
            available_components,
            resource_monitor_actor,
        });
    }

    /// Invokes each registered agent for a given lifespan. If sequential is true,
    /// invocations will only proceed to the next agent once the current
    /// invocation has been successfully acknowledged. When sequential is false,
    /// agents will receive their invocations without waiting. However, the
    /// overall completion (signaled through the receiver returned by the method),
    /// will not return until all invocations have been acknowledged.
    pub async fn execute_lifespan(
        &self,
        lifespan: Lifespan,
        service_context: ServiceContextHandle,
        sequential: bool,
    ) -> Result<(), Error> {
        let mut pending_receptors = Vec::new();

        for &signature in &self.agent_signatures {
            let mut receptor = self
                .messenger
                .message(
                    agent::Payload::Invocation(Invocation {
                        lifespan: lifespan.clone(),
                        service_context: service_context.clone(),
                    }),
                    Audience::Messenger(signature),
                )
                .send();

            if sequential {
                let result = process_payload(receptor.next_payload().await);
                if result.is_err() {
                    return result;
                }
            } else {
                pending_receptors.push(receptor);
            }
        }

        // Pending acks should only be present for non sequential execution. In
        // this case wait for each to complete.
        for mut receptor in pending_receptors {
            let result = process_payload(receptor.next_payload().await);
            if result.is_err() {
                return result;
            }
        }

        Ok(())
    }
}

fn process_payload(
    payload: Result<(agent::Payload, agent::message::Client), Error>,
) -> Result<(), Error> {
    match payload {
        Ok((agent::Payload::Complete(Ok(_)), _)) => Ok(()),
        Ok((agent::Payload::Complete(Err(AgentError::UnhandledLifespan)), _)) => Ok(()),
        _ => Err(format_err!("invocation failed")),
    }
}

#[async_trait]
impl Authority for AuthorityImpl {
    async fn register(&mut self, blueprint: BlueprintHandle) -> Result<(), Error> {
        let (messenger, agent_receptor) = self
            .messenger_factory
            .create(MessengerType::Unbound)
            .await
            .map_err(|_| format_err!("could not register"))?;
        let signature = messenger.get_signature();
        blueprint
            .create(
                Context::new(
                    agent_receptor,
                    blueprint.get_descriptor(),
                    self.switchboard_messenger_factory.clone(),
                    self.event_factory.clone(),
                    self.available_components.clone(),
                    self.resource_monitor_actor.clone(),
                )
                .await,
            )
            .await;

        self.agent_signatures.push(signature);

        Ok(())
    }
}
