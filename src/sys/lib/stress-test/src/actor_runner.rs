// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        actor::{Actor, ActorError},
        counter::CounterTx,
    },
    fuchsia_async::Task,
    futures::lock::Mutex,
    log::debug,
    std::{sync::Arc, thread::sleep, time::Duration},
};

/// The thread that runs an actor indefinitely
#[derive(Clone)]
pub struct ActorRunner {
    // The name of this actor
    pub name: String,

    // The number of seconds to wait between actor operations
    pub delay: u64,

    // A mutable reference to the actor for this configuration.
    // The runner will lock on the actor when it is performing an operation.
    // The environment can lock on the actor during reset.
    pub actor: Arc<Mutex<dyn Actor>>,
}

impl ActorRunner {
    pub fn new<A: Actor>(name: impl ToString, delay: u64, actor: Arc<Mutex<A>>) -> Self {
        Self { name: name.to_string(), delay, actor: actor as Arc<Mutex<dyn Actor>> }
    }

    /// Run the actor on a new thread indefinitely for the given generation.
    /// The runner will stop if the actor requests an environment reset.
    pub fn run(self, counter_tx: CounterTx, generation: u64) -> Task<(ActorRunner, u64)> {
        Task::blocking(async move {
            let mut local_count: u64 = 0;
            loop {
                if self.delay > 0 {
                    debug!(
                        "[{}][{}][{}] Sleeping for {} seconds",
                        generation, self.name, local_count, self.delay
                    );
                    sleep(Duration::from_secs(self.delay));
                }

                debug!("[{}][{}][{}] Performing...", generation, self.name, local_count);

                // Lock on the actor and perform. This prevents the environment from
                // modifying the actor until the operation is complete.
                let result = {
                    let mut actor = self.actor.lock().await;
                    actor.perform().await
                };

                match result {
                    Ok(()) => {
                        // Count this iteration towards the global count
                        let _ = counter_tx.unbounded_send(self.name.clone());
                        debug!("[{}][{}][{}] Done!", generation, self.name, local_count);
                    }
                    Err(ActorError::DoNotCount) => {
                        // Do not count this iteration towards global count
                    }
                    Err(ActorError::ResetEnvironment) => {
                        // Actor needs environment to be reset. Stop the runner
                        debug!(
                            "[{}][{}][{}] Reset Environment!",
                            generation, self.name, local_count
                        );
                        return (self, generation);
                    }
                }

                local_count += 1;
            }
        })
    }
}
