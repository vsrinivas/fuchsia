// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use crate::agent::authority_impl::AuthorityImpl;
use crate::agent::base::{
    AgentError, Authority, BlueprintHandle, Context, Invocation, InvocationResult, Lifespan,
};
use crate::internal::agent;
use crate::internal::event;
use crate::internal::switchboard;
use crate::registry::device_storage::testing::InMemoryStorageFactory;
use crate::service_context::ServiceContext;
use crate::switchboard::base::SettingType;
use crate::tests::scaffold;
use crate::EnvironmentBuilder;
use anyhow::{format_err, Error};
use core::fmt::{Debug, Formatter};
use fuchsia_async as fasync;
use futures::channel::mpsc::UnboundedSender;
use futures::lock::Mutex;
use futures::StreamExt;
use rand::Rng;
use std::collections::HashSet;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_agent_test_environment";

type CallbackSender = UnboundedSender<(u32, Invocation, AckSender)>;
type AckSender = futures::channel::oneshot::Sender<InvocationResult>;

#[derive(PartialEq, Clone)]
enum LifespanTarget {
    Initialization,
    Service,
}

/// Agent provides a test agent to interact with the authority impl. It is
/// instantiated with an id that can be used to identify it when returned by
/// other parts of the code. Additionally, the last invocation is stored so that
/// it can be inspected in tests.
///
/// An asynchronous task is spawned upon creation, which listens to an
/// invocations. Whenever an invocation is encountered, a callback provided at
/// construction is fired (in this context to inform the test of the change). At
/// that point, the agent owner may continue the lifespan execution by calling
/// continue_invocation.
struct TestAgent {
    id: u32,
    lifespan_target: LifespanTarget,
    last_invocation: Option<Invocation>,
    callback: CallbackSender,
}

impl Debug for TestAgent {
    fn fmt(&self, f: &mut Formatter<'_>) -> core::fmt::Result {
        write!(f, "Agent {{ id: {} }}", self.id)
    }
}

impl TestAgent {
    // Creates an agent and spawns a listener for invocation. The agent will be
    // registered with the given authority for the lifespan specified. The
    // callback will be invoked whenever an invocation is encountered, passing a
    // reference to this agent.
    pub async fn create_and_register(
        id: u32,
        lifespan_target: LifespanTarget,
        authority: &mut dyn Authority,
        callback: CallbackSender,
    ) -> Result<Arc<Mutex<TestAgent>>, Error> {
        let (agent, generate) = Self::create(id, lifespan_target, callback);

        if !authority.register(generate).await.is_ok() {
            return Err(format_err!("could not register"));
        }

        Ok(agent)
    }

    pub fn create(
        id: u32,
        lifespan_target: LifespanTarget,
        callback: CallbackSender,
    ) -> (Arc<Mutex<TestAgent>>, BlueprintHandle) {
        let agent = Arc::new(Mutex::new(TestAgent {
            id: id,
            last_invocation: None,
            lifespan_target: lifespan_target,
            callback: callback,
        }));

        let agent_clone = agent.clone();
        let blueprint = Arc::new(scaffold::agent::Blueprint::new(
            scaffold::agent::Generate::Sync(Arc::new(move |mut context: Context| {
                let agent = agent_clone.clone();
                fasync::Task::spawn(async move {
                    while let Ok((payload, client)) = context.receptor.next_payload().await {
                        if let agent::Payload::Invocation(invocation) = payload {
                            client
                                .reply(agent::Payload::Complete(
                                    agent.lock().await.handle(invocation).await,
                                ))
                                .send()
                                .ack();
                        }
                    }
                })
                .detach();
            })),
            Box::leak(id.to_string().into_boxed_str()),
        ));

        (agent.clone(), blueprint)
    }

    async fn handle(&mut self, invocation: Invocation) -> InvocationResult {
        match invocation.lifespan.clone() {
            Lifespan::Initialization => {
                if self.lifespan_target != LifespanTarget::Initialization {
                    return Err(AgentError::UnhandledLifespan);
                }
            }
            Lifespan::Service => {
                if self.lifespan_target != LifespanTarget::Service {
                    return Err(AgentError::UnhandledLifespan);
                }
            }
        }

        self.last_invocation = Some(invocation.clone());
        let (tx, rx) = futures::channel::oneshot::channel::<InvocationResult>();
        self.callback.unbounded_send((self.id, invocation.clone(), tx)).ok();
        if let Ok(result) = rx.await {
            return result;
        } else {
            return Err(AgentError::UnexpectedError);
        }
    }

    /// Returns the id specified at construction time.
    pub fn id(&self) -> u32 {
        return self.id;
    }

    /// Returns the last encountered, unprocessed invocation. None will be
    /// returned if such invocation does not exist.
    pub fn last_invocation(&self) -> Option<Invocation> {
        if let Some(last_invocation) = &self.last_invocation {
            return Some(last_invocation.clone());
        }

        return None;
    }
}

/// Ensures creating environment properly invokes the right lifespans.
#[fuchsia_async::run_until_stalled(test)]
async fn test_environment_startup() {
    let startup_agent_id = 1;
    let (startup_tx, mut startup_rx) =
        futures::channel::mpsc::unbounded::<(u32, Invocation, AckSender)>();

    let service_agent_id = 2;
    let (service_tx, mut service_rx) =
        futures::channel::mpsc::unbounded::<(u32, Invocation, AckSender)>();
    let (service_agent, service_agent_generate) =
        TestAgent::create(service_agent_id, LifespanTarget::Service, service_tx);

    {
        let service_agent = service_agent.clone();
        fasync::Task::spawn(async move {
            // Wait for the initialization agent to receive invocation
            if let Some((id, _, tx)) = startup_rx.next().await {
                // Verify the correct agent was invoked.
                assert_eq!(id, startup_agent_id);
                assert!(tx.send(Ok(())).is_ok());
                // Ensure the service agent hasn't been invoked
                assert!(service_agent.lock().await.last_invocation.is_none());
            }
        })
        .detach();
    }

    fasync::Task::spawn(async move {
        // Wait for service agent to receive notification
        if let Some((id, _, tx)) = service_rx.next().await {
            // Verify the correct agent was invoked
            assert_eq!(id, service_agent_id);
            // Ensure acknowledging succeeds
            assert!(tx.send(Ok(())).is_ok());
        }
    })
    .detach();

    let (_, agent_generate) =
        TestAgent::create(startup_agent_id, LifespanTarget::Initialization, startup_tx);

    assert!(EnvironmentBuilder::new(InMemoryStorageFactory::create())
        .agents(&[service_agent_generate, agent_generate,])
        .settings(&[SettingType::Display])
        .spawn_nested(ENV_NAME)
        .await
        .is_ok());
}

async fn create_authority() -> AuthorityImpl {
    AuthorityImpl::create(
        agent::message::create_hub(),
        switchboard::message::create_hub(),
        event::message::create_hub(),
        HashSet::new(),
    )
    .await
    .unwrap()
}

/// Ensures that agents are executed in sequential order and the
/// completion ack only is sent when all agents have completed.
#[fuchsia_async::run_until_stalled(test)]
async fn test_sequential() {
    let (tx, mut rx) = futures::channel::mpsc::unbounded::<(u32, Invocation, AckSender)>();
    let mut authority = create_authority().await;
    let service_context = ServiceContext::create(None, None);

    // Create a number of agents.
    let agent_ids =
        create_agents(12, LifespanTarget::Initialization, &mut authority, tx.clone()).await;

    fasync::Task::spawn(async move {
        // Process the agent callbacks, making sure they are received in the right
        // order and acknowledging the acks. Note that this is a chain reaction.
        // Processing the first agent is necessary before the second can receive its
        // invocation.
        for agent_id in agent_ids {
            match rx.next().await {
                Some((id, _, tx)) => {
                    assert!(rx.try_next().is_err());

                    if agent_id == id {
                        assert!(tx.send(Ok(())).is_ok());
                    }
                }
                _ => {
                    panic!("couldn't get invocation");
                }
            }
        }
    })
    .detach();

    // Ensure lifespan execution completes.
    assert!(authority
        .execute_lifespan(Lifespan::Initialization, service_context, true,)
        .await
        .is_ok());
}

/// Ensures that in simultaneous execution agents are not blocked on each other
/// and the completion ack waits for all to complete.
#[fuchsia_async::run_until_stalled(test)]
async fn test_simultaneous() {
    let (tx, mut rx) = futures::channel::mpsc::unbounded::<(u32, Invocation, AckSender)>();
    let mut authority = create_authority().await;
    let service_context = ServiceContext::create(None, None);
    let agent_ids =
        create_agents(12, LifespanTarget::Initialization, &mut authority, tx.clone()).await;

    fasync::Task::spawn(async move {
        // Ensure that each agent has received the invocation. Note that we are not
        // acknowledging the invocations here. Each agent should be notified
        // regardless of order.
        let mut senders = Vec::new();
        for agent_id in agent_ids {
            if let Some((id, _, tx)) = rx.next().await {
                assert_eq!(id, agent_id);
                senders.push(tx);
            } else {
                panic!("should be able to retrieve agent");
            }
        }

        // Acknowledge each invocation.
        for sender in senders {
            assert!(sender.send(Ok(())).is_ok());
        }
    })
    .detach();

    // Execute lifespan non-sequentially.
    assert!(authority
        .execute_lifespan(Lifespan::Initialization, service_context, false,)
        .await
        .is_ok());
}

/// Checks that errors returned from an agent stop execution of a lifecycle.
#[fuchsia_async::run_until_stalled(test)]
async fn test_err_handling() {
    let (tx, mut rx) = futures::channel::mpsc::unbounded::<(u32, Invocation, AckSender)>();
    let mut authority = create_authority().await;
    let service_context = ServiceContext::create(None, None);
    let mut rng = rand::thread_rng();

    let agent_1_id = TestAgent::create_and_register(
        rng.gen(),
        LifespanTarget::Initialization,
        &mut authority,
        tx.clone(),
    )
    .await
    .unwrap()
    .lock()
    .await
    .id();

    let agent2_lock = TestAgent::create_and_register(
        rng.gen(),
        LifespanTarget::Initialization,
        &mut authority,
        tx.clone(),
    )
    .await
    .unwrap();

    fasync::Task::spawn(async move {
        // Ensure the first agent received an invocation, acknowledge with an error.
        if let Some((id, _, tx)) = rx.next().await {
            assert_eq!(agent_1_id, id);
            assert!(tx.send(Err(AgentError::UnexpectedError)).is_ok());
        } else {
            panic!("did not receive expected response from agent");
        }
    })
    .detach();

    // Execute lifespan sequentially. Should fail since agent 2 returns an error.
    assert!(authority
        .execute_lifespan(Lifespan::Initialization, service_context, true,)
        .await
        .is_err());

    assert!(agent2_lock.lock().await.last_invocation().is_none());
}

async fn create_agents(
    count: u32,
    lifespan_target: LifespanTarget,
    authority: &mut dyn Authority,
    sender: UnboundedSender<(u32, Invocation, AckSender)>,
) -> Vec<u32> {
    let mut return_agents = Vec::new();
    let mut rng = rand::thread_rng();

    for _i in 0..count {
        let id = rng.gen();
        return_agents.push(id);
        assert!(TestAgent::create_and_register(
            id,
            lifespan_target.clone(),
            authority,
            sender.clone()
        )
        .await
        .is_ok())
    }

    return return_agents;
}
