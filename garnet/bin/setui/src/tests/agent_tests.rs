// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#[cfg(test)]
use crate::agent::authority_impl::AuthorityImpl;
use crate::agent::base::*;
use crate::service_context::ServiceContext;
use anyhow::{format_err, Error};
use futures::channel::mpsc::UnboundedSender;
use futures::lock::Mutex;
use futures::StreamExt;
use rand::Rng;
use std::sync::Arc;

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
    lifespan: Lifespan,
    last_invocation: Option<Invocation>,
    callback: UnboundedSender<(u32, Invocation)>,
}

impl Agent for TestAgent {
    fn invoke(&mut self, invocation: Invocation) -> Result<bool, Error> {
        if invocation.lifespan != self.lifespan {
            return Ok(false);
        }

        self.last_invocation = Some(invocation.clone());
        self.callback.unbounded_send((self.id, invocation.clone())).ok();

        return Ok(true);
    }
}

impl TestAgent {
    // Creates an agent and spawns a listener for invocation. The agent will be
    // registered with the given authority for the lifespan specified. The
    // callback will be invoked whenever an invocation is encountered, passing a
    // reference to this agent.
    pub fn create(
        id: u32,
        lifespan: Lifespan,
        authority: &mut dyn Authority,
        callback: UnboundedSender<(u32, Invocation)>,
    ) -> Result<Arc<Mutex<TestAgent>>, Error> {
        let agent = Arc::new(Mutex::new(TestAgent {
            id: id,
            last_invocation: None,
            lifespan: lifespan,
            callback: callback,
        }));

        if !authority.register(agent.clone()).is_ok() {
            return Err(format_err!("could not register"));
        }

        return Ok(agent.clone());
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

/// Ensures that agents are executed in sequential order and the
/// completion ack only is sent when all agents have completed.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_sequential() {
    let (tx, mut rx) = futures::channel::mpsc::unbounded::<(u32, Invocation)>();
    let mut authority = AuthorityImpl::new();
    let service_context = ServiceContext::create(None);

    // Create a number of agents.
    let agent_ids = create_agents(12, Lifespan::Initialization, &mut authority, tx.clone());

    // Execute the lifespan sequentially.
    let completion_ack =
        authority.execute_lifespan(Lifespan::Initialization, service_context, true);

    // Process the agent callbacks, making sure they are received in the right
    // order and acknowledging the acks. Note that this is a chain reaction.
    // Processing the first agent is necessary before the second can receive its
    // invocation.
    for agent_id in agent_ids {
        match rx.next().await {
            Some((id, invocation)) => {
                assert!(rx.try_next().is_err());

                if agent_id == id {
                    assert!(invocation.acknowledge(Ok(())).await.is_ok());
                }
            }
            _ => {
                panic!("couldn't get invocation");
            }
        }
    }

    // Ensure lifespan execution completes.
    if let Ok(success) = completion_ack.await {
        assert!(success.is_ok());
    } else {
        panic!("did not complete successfully");
    }
}

/// Ensures that in simultaneous execution agents are not blocked on each other
/// and the completion ack waits for all to complete.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_simultaneous() {
    let (tx, mut rx) = futures::channel::mpsc::unbounded::<(u32, Invocation)>();
    let mut authority = AuthorityImpl::new();
    let service_context = ServiceContext::create(None);
    let agent_ids = create_agents(12, Lifespan::Initialization, &mut authority, tx.clone());

    // Execute lifespan non-sequentially.
    let completion_ack =
        authority.execute_lifespan(Lifespan::Initialization, service_context, false);

    // Ensure that each agent has received the invocation. Note that we are not
    // acknowledging the invocations here. Each agent should be notified
    // regardless of order.
    let mut invocations = Vec::new();
    for agent_id in agent_ids {
        if let Some((id, invocation)) = rx.next().await {
            assert_eq!(id, agent_id);
            invocations.push(invocation.clone());
        } else {
            panic!("should be able to retrieve agent");
        }
    }

    // Acknowledge each invocation.
    for invocation in invocations {
        assert!(invocation.acknowledge(Ok(())).await.is_ok());
    }

    // Ensure lifespan execution completes.
    if let Ok(success) = completion_ack.await {
        assert!(success.is_ok());
    } else {
        panic!("did not complete successfully");
    }
}

/// Checks that errors returned from an agent stop execution of a lifecycle.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_err_handling() {
    let (tx, mut rx) = futures::channel::mpsc::unbounded::<(u32, Invocation)>();
    let mut authority = AuthorityImpl::new();
    let service_context = ServiceContext::create(None);
    let mut rng = rand::thread_rng();

    let agent_1_id =
        TestAgent::create(rng.gen(), Lifespan::Initialization, &mut authority, tx.clone())
            .unwrap()
            .lock()
            .await
            .id();

    let agent2_lock =
        TestAgent::create(rng.gen(), Lifespan::Initialization, &mut authority, tx.clone()).unwrap();

    // Execute lifespan sequentially
    let completion_ack =
        authority.execute_lifespan(Lifespan::Initialization, service_context, true);

    // Ensure the first agent received an invocation, acknowledge with an error.
    if let Some((id, invocation)) = rx.next().await {
        assert_eq!(agent_1_id, id);
        assert!(invocation.acknowledge(Err(format_err!("injected error"))).await.is_ok());
    } else {
        panic!("did not receive expected response from agent");
    }

    let completion_result = completion_ack.await;

    // Make sure the completion result could be fetched.
    if completion_result.is_err() {
        panic!("completion ack not properly received");
    }

    // Verify an error was encountered during completion.
    if completion_result.unwrap().is_ok() {
        panic!("an error should have been encountered");
    }

    assert!(agent2_lock.lock().await.last_invocation().is_none());
}

fn create_agents(
    count: u32,
    lifespan: Lifespan,
    authority: &mut dyn Authority,
    sender: UnboundedSender<(u32, Invocation)>,
) -> Vec<u32> {
    let mut return_agents = Vec::new();
    let mut rng = rand::thread_rng();

    for _i in 0..count {
        let id = rng.gen();
        return_agents.push(id);
        assert!(TestAgent::create(id, lifespan, authority, sender.clone()).is_ok())
    }

    return return_agents;
}
