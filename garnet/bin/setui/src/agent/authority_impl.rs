// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::base::*;

use crate::service_context::ServiceContextHandle;
use anyhow::{format_err, Error};
use fuchsia_async as fasync;
use futures::lock::Mutex;
use std::sync::Arc;

type AckReceiver = futures::channel::oneshot::Receiver<Result<(), Error>>;

/// AuthorityImpl is the default implementation of the Authority trait. It
/// provides the ability to execute agents sequentially or simultaneously for a
/// given stage.
pub struct AuthorityImpl {
    // A mapping of lifespans to vectors of agents, ordered by registration
    // sequence.
    agents: Vec<AgentHandle>,
}

/// Waits on an invocation to be acknowledged and verifies whether an error
/// occurred or was returned.
async fn process_invocation_ack(ack_rx: AckReceiver) -> Result<(), Error> {
    let response_result = ack_rx.await;
    if response_result.is_err() || response_result.unwrap().is_err() {
        return Err(anyhow::format_err!("agent failed to acknowledge"));
    }

    return Ok(());
}

impl AuthorityImpl {
    pub fn new() -> AuthorityImpl {
        return AuthorityImpl { agents: Vec::new() };
    }

    /// Invokes each registered agent for a given lifespan. If sequential is true,
    /// invocations will only proceed to the next agent once the current
    /// invocation has been successfully acknowledged. When sequential is false,
    /// agents will receive their invocations without waiting. However, the
    /// overall completion (signaled through the receiver returned by the method),
    /// will not return until all invocations have been acknowledged.
    pub fn execute_lifespan(
        &self,
        lifespan: Lifespan,
        service_context: ServiceContextHandle,
        sequential: bool,
    ) -> futures::channel::oneshot::Receiver<Result<(), Error>> {
        let (completion_tx, completion_rx) =
            futures::channel::oneshot::channel::<Result<(), Error>>();
        let agents = self.agents.clone();

        // Due to waiting on acknowledgements, we must spawn a separate task.
        fasync::spawn(async move {
            let mut pending_acks = Vec::new();

            for agent in agents {
                // Create ack channel.
                let (response_tx, response_rx) =
                    futures::channel::oneshot::channel::<Result<(), Error>>();

                match agent.lock().await.invoke(Invocation {
                    lifespan: lifespan,
                    service_context: service_context.clone(),
                    ack_sender: Arc::new(Mutex::new(Some(response_tx))),
                }) {
                    Ok(handled) => {
                        // did not process result.
                        if !handled {
                            continue;
                        }

                        // Wait for invocation ack is sequential, otherwise store receiver to
                        // be waited on later.
                        if sequential {
                            let result = process_invocation_ack(response_rx).await;
                            // This cannot be part of process_invocation_ack since completion_tx
                            // would be moved.
                            if result.is_err() {
                                completion_tx.send(result).ok();
                                return;
                            }
                        } else {
                            pending_acks.push(response_rx);
                        }
                    }
                    _ => {
                        completion_tx.send(Err(format_err!("failed to invoke agent"))).ok();
                        return;
                    }
                }
            }

            // Pending acks should only be present for non sequential execution. In
            // this case wait for each to complete.
            for ack in pending_acks {
                let result = process_invocation_ack(ack).await;
                if result.is_err() {
                    completion_tx.send(result).ok();
                    return;
                }
            }

            // Acknowledge completion of the execution.
            completion_tx.send(Ok(())).ok();
        });

        return completion_rx;
    }
}

impl Authority for AuthorityImpl {
    fn register(&mut self, agent: AgentHandle) -> Result<(), Error> {
        self.agents.push(agent);
        return Ok(());
    }
}
