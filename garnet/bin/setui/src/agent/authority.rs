// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::{AgentError, BlueprintHandle, Context, Invocation, Lifespan, Payload};
use crate::base::SettingType;
use crate::message::base::{Audience, MessengerType};
use crate::monitor;
use crate::policy::PolicyType;
use crate::service;
use crate::service_context::ServiceContext;
use anyhow::{format_err, Context as _, Error};
use std::collections::HashSet;
use std::sync::Arc;

/// Authority provides the ability to execute agents sequentially or simultaneously for a given
/// stage.
pub struct Authority {
    // This is a list of pairs of debug ids and agent addresses.
    agent_signatures: Vec<(&'static str, service::message::Signature)>,
    // Factory passed to agents for communicating with the service.
    delegate: service::message::Delegate,
    // Messenger
    messenger: service::message::Messenger,
    // Available components
    available_components: HashSet<SettingType>,
    available_policies: HashSet<PolicyType>,
    // Available resource monitors
    resource_monitor_actor: Option<monitor::environment::Actor>,
}

impl Authority {
    pub async fn create(
        delegate: service::message::Delegate,
        available_components: HashSet<SettingType>,
        available_policies: HashSet<PolicyType>,
        resource_monitor_actor: Option<monitor::environment::Actor>,
    ) -> Result<Authority, Error> {
        let (client, _) = delegate
            .create(MessengerType::Unbound)
            .await
            .map_err(|_| anyhow::format_err!("could not create agent messenger for authority"))?;

        Ok(Authority {
            agent_signatures: Vec::new(),
            delegate,
            messenger: client,
            available_components,
            available_policies,
            resource_monitor_actor,
        })
    }

    pub async fn register(&mut self, blueprint: BlueprintHandle) {
        let agent_receptor = self
            .delegate
            .create(MessengerType::Unbound)
            .await
            .expect("agent receptor should be created")
            .1;
        let signature = agent_receptor.get_signature();
        blueprint
            .create(
                Context::new(
                    agent_receptor,
                    self.delegate.clone(),
                    self.available_components.clone(),
                    self.available_policies.clone(),
                    self.resource_monitor_actor.clone(),
                )
                .await,
            )
            .await;

        self.agent_signatures.push((blueprint.debug_id(), signature));
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
        service_context: Arc<ServiceContext>,
        sequential: bool,
    ) -> Result<(), Error> {
        let mut pending_receptors = Vec::new();

        for &(debug_id, signature) in &self.agent_signatures {
            let mut receptor = self
                .messenger
                .message(
                    Payload::Invocation(Invocation {
                        lifespan,
                        service_context: Arc::clone(&service_context),
                    })
                    .into(),
                    Audience::Messenger(signature),
                )
                .send();

            if sequential {
                let result = process_payload(debug_id, receptor.next_of::<Payload>().await);
                if result.is_err() {
                    return result;
                }
            } else {
                pending_receptors.push((debug_id, receptor));
            }
        }

        // Pending acks should only be present for non sequential execution. In
        // this case wait for each to complete.
        for (debug_id, mut receptor) in pending_receptors {
            let result = process_payload(debug_id, receptor.next_of::<Payload>().await);
            if result.is_err() {
                return result;
            }
        }

        Ok(())
    }
}

fn process_payload(
    debug_id: &str,
    payload: Result<(Payload, service::message::MessageClient), Error>,
) -> Result<(), Error> {
    match payload {
        Ok((Payload::Complete(Ok(_) | Err(AgentError::UnhandledLifespan)), _)) => Ok(()),
        Ok((Payload::Complete(result), _)) => {
            result.with_context(|| format!("Invocation failed for {:?}", debug_id))
        }
        Ok(_) => Err(format_err!("Unexpected result for {:?}", debug_id)),
        Err(e) => Err(e).with_context(|| format!("Invocation failed {:?}", debug_id)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::agent::Blueprint;
    use crate::message::message_hub::MessageHub;
    use fuchsia_async as fasync;
    use matches::assert_matches;

    #[fasync::run_until_stalled(test)]
    async fn test_log() {
        struct TestAgentBlueprint;

        impl Blueprint for TestAgentBlueprint {
            fn debug_id(&self) -> &'static str {
                "test_agent"
            }

            fn create(&self, context: Context) -> futures::future::BoxFuture<'static, ()> {
                Box::pin(async move {
                    let mut receptor = context.receptor;
                    fasync::Task::spawn(async move {
                        while let Ok((Payload::Invocation(_), client)) =
                            receptor.next_of::<Payload>().await
                        {
                            client
                                .reply(Payload::Complete(Err(AgentError::UnexpectedError)).into())
                                .send()
                                .ack();
                        }
                    })
                    .detach();
                })
            }
        }

        let delegate = MessageHub::create(None);
        let mut authority = Authority::create(delegate, HashSet::new(), HashSet::new(), None)
            .await
            .expect("Should be able to create authority");
        let agent = TestAgentBlueprint;
        authority.register(Arc::new(agent)).await;
        let result = authority
            .execute_lifespan(
                Lifespan::Initialization,
                Arc::new(ServiceContext::new(None, None)),
                false,
            )
            .await;
        assert_matches!(result, Err(e) if format!("{:?}", e).contains("test_agent"));
    }
}
